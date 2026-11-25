# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import asyncio
import struct
import unittest
from unittest.mock import patch

import pywatchman_aio
from pywatchman import bser, pybser, WatchmanError


class TestAsyncBserCodec(unittest.IsolatedAsyncioTestCase):
    def make_frame(self, value, version, integer_code, integer_format):
        payload = pybser.dumps(value)[7:]
        prefix = bytes((0, version))
        if version == 2:
            prefix += struct.pack("=I", 0)
        return (
            prefix
            + bytes((integer_code,))
            + struct.pack("=" + integer_format, len(payload))
            + payload
        )

    def make_codec(self):
        reader = asyncio.StreamReader()
        transport = pywatchman_aio.AsyncUnixSocketTransport()
        transport.reader = reader
        return reader, pywatchman_aio.AsyncBserCodec(transport)

    async def test_fragmented_header(self):
        for decoder in (bser, pybser):
            for version in (1, 2):
                for integer_code, integer_format in (
                    (3, "b"),
                    (4, "h"),
                    (5, "i"),
                    (6, "q"),
                ):
                    with self.subTest(
                        decoder=decoder.__name__,
                        version=version,
                        integer_code=integer_code,
                    ), patch.object(pywatchman_aio, "bser", decoder):
                        frame = self.make_frame(
                            {"value": 42}, version, integer_code, integer_format
                        )
                        reader, codec = self.make_codec()
                        reader.feed_data(frame[:1])
                        receive = asyncio.create_task(codec.receive())
                        await asyncio.sleep(0)
                        reader.feed_data(frame[1:])
                        reader.feed_eof()
                        self.assertEqual(await receive, {"value": 42})

    async def test_consecutive_short_frames(self):
        for decoder in (bser, pybser):
            for version in (1, 2):
                for integer_code, integer_format in (
                    (3, "b"),
                    (4, "h"),
                    (5, "i"),
                    (6, "q"),
                ):
                    with self.subTest(
                        decoder=decoder.__name__,
                        version=version,
                        integer_code=integer_code,
                    ), patch.object(pywatchman_aio, "bser", decoder):
                        reader, codec = self.make_codec()
                        reader.feed_data(
                            self.make_frame(True, version, integer_code, integer_format)
                            + self.make_frame(
                                False, version, integer_code, integer_format
                            )
                        )
                        reader.feed_eof()
                        self.assertIs(await codec.receive(), True)
                        self.assertIs(await codec.receive(), False)

    async def test_fragmented_body(self):
        for decoder in (bser, pybser):
            with self.subTest(decoder=decoder.__name__), patch.object(
                pywatchman_aio, "bser", decoder
            ):
                value = {"value": "a" * 100}
                frame = self.make_frame(value, 2, 5, "i")
                reader, codec = self.make_codec()
                reader.feed_data(frame[:15])
                receive = asyncio.create_task(codec.receive())
                await asyncio.sleep(0)
                reader.feed_data(frame[15:])
                reader.feed_eof()
                self.assertEqual(await receive, value)

    async def test_truncated_frames(self):
        for version in (1, 2):
            frame = self.make_frame({"value": 42}, version, 6, "q")
            for end in range(len(frame)):
                with self.subTest(version=version, end=end):
                    reader, codec = self.make_codec()
                    reader.feed_data(frame[:end])
                    reader.feed_eof()
                    with self.assertRaises(ConnectionResetError):
                        await codec.receive()

    async def test_invalid_headers(self):
        for header in (b"xx", b"\x00\x03", b"\x00\x01\x07"):
            with self.subTest(header=header):
                reader, codec = self.make_codec()
                reader.feed_data(header)
                reader.feed_eof()
                with self.assertRaises(WatchmanError):
                    await codec.receive()


if __name__ == "__main__":
    unittest.main()
