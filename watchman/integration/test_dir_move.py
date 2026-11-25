# vim:ts=4:sw=4:et:
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe


import os
import os.path
import shutil
import time

from watchman.integration.lib import WatchmanTestCase


@WatchmanTestCase.expand_matrix
class TestDirMove(WatchmanTestCase.WatchmanTestCase):
    # testing this is flaky at best on windows due to latency
    # and exclusivity of file handles, so skip it.
    def checkOSApplicability(self) -> None:
        if os.name == "nt":
            self.skipTest("windows is too flaky for this test")

    def build_under(self, root, name, latency: float = 0) -> None:
        os.mkdir(os.path.join(root, name))
        if latency > 0:
            time.sleep(latency)
        self.touch(os.path.join(root, name, "a"))

    def test_atomicMove(self) -> None:
        root = self.mkdtemp()

        dir_of_interest = os.path.join(root, "dir")
        alt_dir = os.path.join(root, "alt")
        dead_dir = os.path.join(root, "bye")

        self.build_under(root, "dir")
        self.watchmanCommand("watch", root)
        self.assertFileList(root, ["dir", "dir/a"])

        # build out a replacement dir
        self.build_under(root, "alt")

        os.rename(dir_of_interest, dead_dir)
        os.rename(alt_dir, dir_of_interest)

        self.assertFileList(root, ["dir", "dir/a", "bye", "bye/a"])

    def test_NonAtomicMove(self) -> None:
        root = self.mkdtemp()

        dir_of_interest = os.path.join(root, "dir")

        self.build_under(root, "dir")
        self.watchmanCommand("watch", root)
        self.assertFileList(root, ["dir", "dir/a"])

        shutil.rmtree(dir_of_interest)
        self.build_under(root, "dir", latency=1)

        self.assertFileList(root, ["dir", "dir/a"])

    def test_renameDirThenCreateChild(self) -> None:
        # Regression test: after renaming a watched directory in-tree,
        # file operations inside the renamed directory must be reported
        # under the new parent name, not the pre-rename path.
        #
        # On Linux/inotify, the IN_MOVED_TO handler previously had an
        # impossible mask check that left the watcher's wd -> name map
        # stale after an in-tree directory rename, so subsequent
        # IN_CREATE/IN_DELETE events on the renamed directory's children
        # were synthesized under the old parent name and surfaced as
        # phantom entries.
        root = self.mkdtemp()
        old_dir = os.path.join(root, "src")
        new_dir = os.path.join(root, "dst")

        os.mkdir(old_dir)
        self.touchRelative(old_dir, "a")

        self.watchmanCommand("watch", root)
        self.assertFileList(root, ["src", "src/a"])

        clock = self.watchmanCommand("clock", root)["clock"]

        os.rename(old_dir, new_dir)
        self.touchRelative(new_dir, "b")

        # The settled file list must reflect the rename and the new child.
        self.assertFileList(root, ["dst", "dst/a", "dst/b"])

        # The since-cursor change set must include dst/b and must not
        # contain a phantom src/b that would appear if the wd -> name
        # map were stale.
        res = self.watchmanCommand(
            "query",
            root,
            {"since": clock, "expression": ["exists"], "fields": ["name"]},
        )
        names = set(res["files"])
        self.assertIn("dst/b", names)
        self.assertNotIn("src/b", names)
        self.assertNotIn("src/a", names)
