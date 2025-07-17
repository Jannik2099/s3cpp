#pragma once

#include <boost/url/url.hpp>
#include <string>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws::iam {

class Session {
public:
    std::string access_key;
    std::string secret_access_key;
    std::string region;
    boost::urls::url endpoint;
};

} // namespace s3cpp::aws::iam

//
#include "s3cpp/internal/macro-end.hpp"
