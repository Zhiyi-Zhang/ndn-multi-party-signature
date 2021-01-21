#ifndef NDNMPS_COMMON_HPP
#define NDNMPS_COMMON_HPP

#include <cstdint>
#include <iostream>
#include <ndn-cxx/encoding/tlv.hpp>

#ifdef NDNMPS_HAVE_TESTS
#define NDNMPS_VIRTUAL_WITH_TESTS virtual
#define NDNMPS_PUBLIC_WITH_TESTS_ELSE_PROTECTED public
#define NDNMPS_PUBLIC_WITH_TESTS_ELSE_PRIVATE public
#define NDNMPS_PROTECTED_WITH_TESTS_ELSE_PRIVATE protected
#else
#define NDNMPS_VIRTUAL_WITH_TESTS
#define NDNMPS_PUBLIC_WITH_TESTS_ELSE_PROTECTED protected
#define NDNMPS_PUBLIC_WITH_TESTS_ELSE_PRIVATE private
#define NDNMPS_PROTECTED_WITH_TESTS_ELSE_PRIVATE private
#endif

namespace ndn {
namespace tlv {

enum : uint32_t {
  MpsSignerList = 200,
  Status = 203,
  UnsignedWrapperName = 205,
  ResultAfter = 209,
  ResultName = 211,
};

/** @brief Extended SignatureType values with Multi-Party Signature
 *  @sa https://named-data.net/doc/NDN-packet-spec/current/signature.html
 */
enum MpsSignatureTypeValue : uint16_t {
  SignatureSha256WithBls = 64,
};

std::ostream&
operator<<(std::ostream& os, MpsSignatureTypeValue st);
}  // namespace tlv

enum class ReplyCode : int {
  Processing = 102,
  OK = 200,
  BadRequest = 400,
  Unauthorized = 401,
  NotFound = 404,
  FailedDependency = 424,
  InternalError = 500,
  Unavailable = 503,
};

/**
 * @brief The extended type of a cryptographic key with Multi-Party Signature.
 */
enum class MpsKeyType {
  NONE = 0,  ///< Unknown or unsupported key type
  RSA,       ///< RSA key, supports sign/verify and encrypt/decrypt operations
  EC,        ///< Elliptic Curve key (e.g. for ECDSA), supports sign/verify operations
  BLS,       ///< BLS key, supports sign/verify operations
  AES,       ///< AES key, supports encrypt/decrypt operations
  HMAC,      ///< HMAC key, supports sign/verify operations
};

std::ostream&
operator<<(std::ostream& os, MpsKeyType keyType);
}  // namespace ndn

#endif  // NDNMPS_COMMON_HPP