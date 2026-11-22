/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This software may be used and distributed according to the terms of the
 * GNU General Public License version 2.
 */

#pragma once

#include "eden/common/utils/PathFuncs.h"

#include <folly/Range.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <thrift/lib/cpp2/protocol/detail/protocol_methods.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace apache::thrift {
class BinaryProtocolWriter;
class CompactProtocolWriter;
} // namespace apache::thrift

namespace facebook::eden {

/**
 * A glob result path that shares directory storage between sibling results.
 *
 * Glob traversal commonly returns many entries from the same directory.
 * GlobPath stores that directory, including its trailing separator, in shared
 * immutable storage and owns only the basename for each result. Comparisons,
 * string conversion, and Thrift serialization behave as if the two pieces
 * were one contiguous relative path.
 *
 * GlobPathBuilder should be used during traversal so sibling paths reuse the
 * same directory allocation.
 */
class GlobPath {
 public:
  GlobPath() : prefix_{emptyPrefix()} {}

  explicit GlobPath(RelativePathPiece path)
      : prefix_{makeDir(path.dirname())},
        basename_{PathComponent{path.basename()}.intoStorage()} {}

  explicit GlobPath(const std::string& path)
      : prefix_{nullptr},
        basename_{PathComponent::storage_type{path.data(), path.size()}} {}

  explicit GlobPath(PathComponent::storage_type&& path)
      : prefix_{nullptr}, basename_{std::move(path)} {}

  GlobPath(
      std::shared_ptr<const std::string> prefix,
      PathComponentPiece basename)
      : prefix_{std::move(prefix)},
        basename_{PathComponent{basename}.intoStorage()} {}

  GlobPath(std::shared_ptr<const std::string> prefix, PathComponent&& basename)
      : prefix_{std::move(prefix)},
        basename_{std::move(basename).intoStorage()} {}

  folly::StringPiece dir() const noexcept {
    if (!prefix_) {
      const auto offset = flatBasenameOffset();
      return offset == 0 ? folly::StringPiece{}
                         : folly::StringPiece{basename_.data(), offset - 1};
    }
    if (prefix_->empty()) {
      return folly::StringPiece{};
    }
    return folly::StringPiece{prefix_->data(), prefix_->size() - 1};
  }

  folly::StringPiece basename() const noexcept {
    if (!prefix_) {
      const auto offset = flatBasenameOffset();
      return folly::StringPiece{
          basename_.data() + offset, basename_.size() - offset};
    }
    return folly::StringPiece{basename_};
  }

  size_t size() const noexcept {
    if (!prefix_) {
      return basename_.size();
    }
    return prefix_->size() + basename_.size();
  }

  PathComponent::storage_type intoFbString() && {
    if (!prefix_ || prefix_->empty()) {
      return std::move(basename_);
    }

    PathComponent::storage_type path;
    path.reserve(size());
    path.append(prefix_->data(), prefix_->size());
    path.append(basename_);
    return path;
  }

  std::string asString() const {
    if (!prefix_ || prefix_->empty()) {
      return std::string{basename_.data(), basename_.size()};
    }

    std::string path;
    path.reserve(size());
    path.append(*prefix_);
    path.append(basename_);
    return path;
  }

  /**
   * Returns a non-owning buffer over this path's storage.
   *
   * The GlobPath must outlive all access to the returned buffer.
   */
  folly::IOBuf toIOBuf() const {
    if (!prefix_ || prefix_->empty()) {
      return folly::IOBuf::wrapBufferAsValue(
          basename_.data(), basename_.size());
    }

    auto head =
        folly::IOBuf::wrapBufferAsValue(prefix_->data(), prefix_->size());
    head.appendToChain(folly::IOBuf::wrapBuffer(basename()));
    return head;
  }

  bool operator==(const GlobPath& other) const noexcept {
    return compare(other) == 0;
  }

  bool operator!=(const GlobPath& other) const noexcept {
    return !(*this == other);
  }

  bool operator<(const GlobPath& other) const noexcept {
    return compare(other) < 0;
  }

  int compare(const GlobPath& other) const noexcept {
    if (!prefix_ && !other.prefix_) {
      return comparePiece(
          folly::StringPiece{basename_}, folly::StringPiece{other.basename_});
    }
    if (prefix_ == other.prefix_) {
      return comparePiece(basename(), other.basename());
    }

    if (prefix_ && other.prefix_ && *prefix_ == *other.prefix_) {
      return comparePiece(basename(), other.basename());
    }

    return compareComposite(other);
  }

  static std::shared_ptr<const std::string> makeDir(RelativePathPiece path) {
    if (path.empty()) {
      return emptyPrefix();
    }

    std::string prefix;
    prefix.reserve(path.view().size() + 1);
    prefix.append(path.view());
    prefix.push_back(kDirSeparator);
    return std::make_shared<const std::string>(std::move(prefix));
  }

  static std::shared_ptr<const std::string> childDir(
      const std::shared_ptr<const std::string>& prefix,
      PathComponentPiece child) {
    std::string path;
    path.reserve(prefix->size() + child.view().size() + 1);
    path.append(*prefix);
    path.append(child.view());
    path.push_back(kDirSeparator);
    return std::make_shared<const std::string>(std::move(path));
  }

 private:
  size_t flatBasenameOffset() const noexcept {
    for (size_t i = basename_.size(); i > 0; --i) {
      if (detail::isDirSeparator(basename_[i - 1])) {
        return i;
      }
    }
    return 0;
  }

  folly::StringPiece prefix() const noexcept {
    return folly::StringPiece{prefix_->data(), prefix_->size()};
  }

  static int comparePiece(
      folly::StringPiece lhs,
      folly::StringPiece rhs) noexcept {
    const auto result = lhs.compare(rhs);
    if (result != 0) {
      return result < 0 ? -1 : 1;
    }
    return 0;
  }

  static int comparePrefix(
      folly::StringPiece lhs,
      folly::StringPiece rhs) noexcept {
    const auto minSize = std::min(lhs.size(), rhs.size());
    const auto result =
        std::char_traits<char>::compare(lhs.data(), rhs.data(), minSize);
    if (result != 0) {
      return result < 0 ? -1 : 1;
    }
    return 0;
  }

  size_t pieces(std::array<folly::StringPiece, 2>& out) const noexcept {
    if (!prefix_ || prefix_->empty()) {
      out[0] = folly::StringPiece{basename_};
      return 1;
    }
    out[0] = prefix();
    out[1] = basename();
    return 2;
  }

  int compareComposite(const GlobPath& other) const noexcept {
    std::array<folly::StringPiece, 2> lhsPieces;
    std::array<folly::StringPiece, 2> rhsPieces;
    const auto lhsCount = pieces(lhsPieces);
    const auto rhsCount = other.pieces(rhsPieces);
    size_t lhsPiece = 0;
    size_t rhsPiece = 0;
    size_t lhsOffset = 0;
    size_t rhsOffset = 0;

    while (lhsPiece < lhsCount && rhsPiece < rhsCount) {
      const auto lhs = lhsPieces[lhsPiece].subpiece(lhsOffset);
      const auto rhs = rhsPieces[rhsPiece].subpiece(rhsOffset);
      const auto minSize = std::min(lhs.size(), rhs.size());
      const auto result = comparePrefix(lhs, rhs);
      if (result != 0) {
        return result;
      }

      lhsOffset += minSize;
      rhsOffset += minSize;

      if (lhsOffset == lhsPieces[lhsPiece].size()) {
        ++lhsPiece;
        lhsOffset = 0;
      }
      if (rhsOffset == rhsPieces[rhsPiece].size()) {
        ++rhsPiece;
        rhsOffset = 0;
      }
    }

    if (lhsPiece == lhsCount && rhsPiece == rhsCount) {
      return 0;
    }
    return lhsPiece == lhsCount ? -1 : 1;
  }

  static std::shared_ptr<const std::string> emptyPrefix() {
    static const auto dir = std::make_shared<const std::string>();
    return dir;
  }

  std::shared_ptr<const std::string> prefix_;
  PathComponent::storage_type basename_;
};

using GlobPathList = std::vector<GlobPath>;

class GlobPathBuilder {
 public:
  using Dir = std::shared_ptr<const std::string>;

  Dir makeDir(RelativePathPiece path) const {
    return GlobPath::makeDir(path);
  }

  Dir childDir(const Dir& dir, PathComponentPiece child) const {
    return GlobPath::childDir(dir, child);
  }

  GlobPath makePath(const Dir& dir, PathComponentPiece basename) const {
    return GlobPath{dir, basename};
  }

  GlobPath makePath(const Dir& dir, PathComponent&& basename) const {
    return GlobPath{dir, std::move(basename)};
  }
};

} // namespace facebook::eden

namespace apache::thrift::detail::pm {

template <typename ExpectedTag>
struct protocol_methods<
    type_class::binary,
    facebook::eden::GlobPath,
    ExpectedTag> {
  template <typename Protocol>
  static void read(Protocol& protocol, facebook::eden::GlobPath& out) {
    facebook::eden::PathComponent::storage_type path;
    protocol.readBinary(path);
    out = facebook::eden::GlobPath{std::move(path)};
  }

  template <typename Protocol>
  static std::size_t write(
      Protocol& protocol,
      const facebook::eden::GlobPath& in) {
    auto buf = in.toIOBuf();
    return protocol.writeBinary(buf);
  }

  template <bool ZeroCopy, typename Protocol>
  static std::size_t serializedSize(
      Protocol& protocol,
      const facebook::eden::GlobPath& in) {
    using ProtocolType = std::remove_cv_t<std::remove_reference_t<Protocol>>;
    if constexpr (
        std::is_same_v<ProtocolType, BinaryProtocolWriter> ||
        std::is_same_v<ProtocolType, CompactProtocolWriter>) {
      const auto dataSize = in.size();
      const auto sizePrefix = protocol.serializedSizeI32();
      if constexpr (ZeroCopy) {
        return sizePrefix +
            (dataSize <= folly::IOBufQueue::kMaxPackCopy ? dataSize : 0);
      }
      if (dataSize <= std::numeric_limits<uint32_t>::max() - sizePrefix) {
        return sizePrefix + dataSize;
      }
    }

    auto buf = in.toIOBuf();
    if constexpr (ZeroCopy) {
      return protocol.serializedSizeZCBinary(buf);
    } else {
      return protocol.serializedSizeBinary(buf);
    }
  }
};

} // namespace apache::thrift::detail::pm
