#pragma once

#include <boost/describe/class.hpp>
#include <boost/describe/enum.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <vector>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws::s3 {

struct Owner {
    std::optional<std::string> DisplayName;
    std::optional<std::string> ID;

    BOOST_DESCRIBE_STRUCT(Owner, (), (DisplayName, ID));
};

struct RestoreStatus {
    std::optional<bool> IsRestoreInProgress;
    std::optional<std::chrono::time_point<std::chrono::system_clock>> RestoreExpiryDate;

    BOOST_DESCRIBE_STRUCT(RestoreStatus, (), (IsRestoreInProgress, RestoreExpiryDate));
};

struct Object {
    enum class ChecksumAlgorithm : std::uint8_t { CRC32, CRC32C, SHA1, SHA256, CRC64NVME };
    BOOST_DESCRIBE_NESTED_ENUM(ChecksumAlgorithm, CRC32, CRC32C, SHA1, SHA256, CRC64NVME);

    enum class ChecksumType : std::uint8_t { COMPOSITE, FULL_OBJECT };
    BOOST_DESCRIBE_NESTED_ENUM(ChecksumType, COMPOSITE, FULL_OBJECT);

    enum class StorageClass : std::uint8_t {
        STANDARD,
        REDUCED_REDUNDANCY,
        GLACIER,
        STANDARD_IA,
        ONEZONE_IA,
        INTELLIGENT_TIERING,
        DEEP_ARCHIVE,
        OUTPOSTS,
        GLACIER_IR,
        SNOW,
        EXPRESS_ONEZONE,
        FSX_OPENZFS
    };
    BOOST_DESCRIBE_NESTED_ENUM(StorageClass, STANDARD, REDUCED_REDUNDANCY, GLACIER, STANDARD_IA, ONEZONE_IA,
                               INTELLIGENT_TIERING, DEEP_ARCHIVE, OUTPOSTS, GLACIER_IR, SNOW, EXPRESS_ONEZONE,
                               FSX_OPENZFS);

    std::optional<ChecksumAlgorithm> ChecksumAlgorithm_;
    std::optional<ChecksumType> ChecksumType_;
    std::optional<std::string> ETag;
    std::optional<std::string> Key;
    std::optional<std::chrono::time_point<std::chrono::system_clock>> LastModified;
    std::optional<Owner> Owner_;
    std::optional<RestoreStatus> RestoreStatus_;
    std::optional<std::size_t> Size;
    std::optional<StorageClass> StorageClass_;

    BOOST_DESCRIBE_STRUCT(Object, (),
                          (ChecksumAlgorithm_, ChecksumType_, ETag, Key, LastModified, Owner_, RestoreStatus_,
                           Size, StorageClass_));

    [[nodiscard]] explicit Object(std::string_view xml);
    [[nodiscard]] explicit Object(const pugi::xml_node &xml);
};

struct CommonPrefix {
    std::optional<std::string> Prefix;

    BOOST_DESCRIBE_STRUCT(CommonPrefix, (), (Prefix));
};

struct ListBucketResult {
    std::optional<std::vector<CommonPrefix>> CommonPrefixes;
    std::optional<std::vector<Object>> Contents;
    std::optional<std::string> ContinuationToken;
    std::optional<std::string> Delimiter;
    std::optional<std::string> EncodingType;
    bool IsTruncated{};
    std::size_t KeyCount{};
    std::size_t MaxKeys{};
    std::string Name;
    std::optional<std::string> NextContinuationToken;
    std::string Prefix;
    std::optional<std::string> StartAfter;

    BOOST_DESCRIBE_STRUCT(ListBucketResult, (),
                          (CommonPrefixes, Contents, ContinuationToken, Delimiter, EncodingType, IsTruncated,
                           KeyCount, MaxKeys, Name, NextContinuationToken, Prefix, StartAfter));
};

} // namespace s3cpp::aws::s3

//
#include "s3cpp/internal/macro-end.hpp"
