#ifndef NDNMPS_COMMON_HPP
#define NDNMPS_COMMON_HPP

#include <cstdint>
#include <iostream>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/security/certificate.hpp>
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
namespace mps {
namespace tlv {

/**
 * Custom tlv types for Multi-signature protocol packet encoding
 */
enum : uint32_t {
  EcdhPub = 145,
  Salt = 149,
  InitializationVector = 157,
  EncryptedPayload = 159,
  AuthenticationTag = 175,

  MpsSignerList = 200,
  Status = 203,
  ParameterDataName = 205,
  ResultAfter = 209,
  ResultName = 211,
  BLSSigValue = 213
};

/** @brief Extended SignatureType values with Multi-Party Signature
 *  @sa https://named-data.net/doc/NDN-packet-spec/current/signature.html
 */
enum MpsSignatureTypeValue : uint16_t {
  SignatureSha256WithBls = 64,
};

}  // namespace tlv

/**
 * The HTTP-like reply status code for Multi-signature protocol packet encoding
 */
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

} // namespace mps
} // namespace ndn
#endif  // NDNMPS_COMMON_HPP