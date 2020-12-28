#include "ndnmps/schema.hpp"
#include "multi-party-key-locator.hpp"
#include <mcl/bn_c384_256.h>
#include <bls/bls.hpp>
#include <ndn-cxx/data.hpp>

#include <iostream>
#include <map>

namespace ndn {

class Signer {
private:
  blsSecretKey m_sk;
  blsPublicKey m_pk;

public:
  /**
   * @brief Generate public and secret key pair.
   */
  Signer();

  /**
   * @brief Initialize public and secret key pair from secret key serialization.
   */
  Signer(Buffer secretKeyBuf);

  /**
   * @brief Get public key.
   */
  blsPublicKey
  getPublicKey();

  /**
   * @brief Generate public key for network transmission.
   */
  std::vector<uint8_t>
  getpublicKeyStr();

  /**
   * Return the signature value for the packet.
   * @param data the unsigned data packet
   * @param sigInfo the signature info to be used
   * @return the signature value signed by this signer
   */
  Block
  getSignature(Data data, const SignatureInfo& sigInfo);
};

class Verifier {
private:
  std::map<Name, blsPublicKey> m_certs;

public:

  Verifier();

  void
  addCert(const Name& keyName, blsPublicKey pk);

  bool
  verifySignature(const Data& data, const MultipartySchema& schema);

public:
  static bool
  verifyKeyLocator(const MultiPartyKeyLocator& locator, const MultipartySchema& schema);
};

typedef function<void(const Data& signedData)> SignatureFinishCallback;
typedef function<void(const Data& unfinishedData, const std::string& reason)> SignatureFailureCallback;

class Initiator {
public:

  Initiator();

  void
  startSigningProcess(const MultipartySchema& schema, const Data& unfinishedData,
                      const SignatureFinishCallback& successCb, const SignatureFailureCallback& failureCab);

private:
  void
  onTimeout();

  void
  onNack();

  void
  onData();
};

}  // namespace ndn