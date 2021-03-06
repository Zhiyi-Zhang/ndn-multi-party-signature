#include "ndnmps/crypto-players.hpp"
#include "test-common.hpp"

namespace ndn {
namespace mps {
namespace tests {

BOOST_AUTO_TEST_SUITE(TestCryptoPlayers)

BOOST_AUTO_TEST_CASE(TestSignerPublicKey)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), "/a/b/c");
  auto pub = signer.getPublicKey();
  auto pubStr = signer.getpublicKeyStr();
  blsPublicKey pub2;
  BOOST_ASSERT(blsPublicKeyDeserialize(&pub2, pubStr.data(), pubStr.size()) != 0);
  BOOST_ASSERT(blsPublicKeyIsEqual(&pub, &pub2));
}

BOOST_AUTO_TEST_CASE(TestSignerVerifier)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), WildCardName("/a/b/c"));
  auto pub = signer.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert("/a/b/c", pub);

  Data data1;
  data1.setName(Name("/a/b/c/d"));
  data1.setContent(Name("/1/2/3/4").wireEncode());

  MultipartySchema schema;
  schema.signers.emplace_back(WildCardName(signer.getSignerKeyName()));

  signer.sign(data1);
  BOOST_CHECK(verifier.verifySignature(data1, schema));
  BOOST_CHECK_EQUAL(data1.getSignatureValue().value_size(), blsGetSerializedSignatureByteSize());
}

BOOST_AUTO_TEST_CASE(TestSignerVerifierBadKey)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), WildCardName("/a/b/c"));
  auto pub = signer.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert("/a/b/c", pub);

  Data data1;
  data1.setName(Name("/a/b/c/d"));
  data1.setContent(Name("/1/2/3/4").wireEncode());

  MultipartySchema schema;
  schema.signers.emplace_back(WildCardName("/q/w/e/r"));

  signer.sign(data1);
  BOOST_ASSERT(!verifier.verifySignature(data1, schema));
}

BOOST_AUTO_TEST_CASE(TestSignerVerifierBadSig)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), WildCardName("/a/b/c"));
  auto pub = signer.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert("/a/b/c", pub);

  Data data1;
  data1.setName(Name("/a/b/c/d"));
  data1.setContent(Name("/1/2/3/4").wireEncode());

  MultipartySchema schema;
  schema.signers.emplace_back(WildCardName(WildCardName(signer.getSignerKeyName())));

  signer.sign(data1);
  data1.setContent(Name("/1/2/3/4/5").wireEncode());  //changed content
  BOOST_ASSERT(!verifier.verifySignature(data1, schema));
}

BOOST_AUTO_TEST_CASE(TestSignerVerifierInterest)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), WildCardName("/a/b/c"));
  auto pub = signer.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert("/a/b/c", pub);

  Interest interest;
  interest.setName(Name("/a/b/c/d"));
  interest.setApplicationParameters(Name("/1/2/3/4").wireEncode());

  signer.sign(interest);
  BOOST_CHECK(verifier.verifySignature(interest));
  BOOST_CHECK_EQUAL(interest.getSignatureValue().value_size(), blsGetSerializedSignatureByteSize());
}

BOOST_AUTO_TEST_CASE(TestSignerVerifierInterestBadSig)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), WildCardName("/a/b/c"));
  auto pub = signer.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert("/a/b/c", pub);

  Interest interest;
  interest.setName(Name("/a/b/c/d"));
  interest.setApplicationParameters(Name("/1/2/3/4").wireEncode());

  MpsSigner signer2("/a/b/c");
  signer2.sign(interest);
  BOOST_CHECK(!verifier.verifySignature(interest));
  BOOST_CHECK_EQUAL(interest.getSignatureValue().value_size(), blsGetSerializedSignatureByteSize());
}

BOOST_AUTO_TEST_CASE(TestSignerVerifierSelfCert)
{
  MpsSigner signer("/a/b/c/KEY/1234");
  auto pub = signer.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert(signer.getSignerKeyName(), pub);

  MultipartySchema schema;
  schema.signers.emplace_back(signer.getSignerKeyName());

<<<<<<< HEAD:temp/crypto-players.t.cpp
  auto cert = signer.getSelfSignCert(security::ValidityPeriod(time::system_clock::now() - time::seconds(1),
                                                              time::system_clock::now() + time::days(100)));
  BOOST_CHECK(MpsVerifier::verifySignature(cert, cert));
  BOOST_CHECK(verifier.verifySignature(cert, schema));

  MpsVerifier loader;
  BOOST_CHECK_NO_THROW(loader.addCert(cert));
  BOOST_CHECK(MpsVerifier::verifySignature(cert, cert));
}

BOOST_AUTO_TEST_CASE(TestSignerVerifierSelfBadCert)
{
  MpsSigner signer("/a/b/c/KEY/1234");
  auto pub = signer.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert(signer.getSignerKeyName(), pub);

  MultipartySchema schema;
  schema.signers.emplace_back(signer.getSignerKeyName());

  BOOST_CHECK(!verifier.verifySignature(signer.getSelfSignCert(security::ValidityPeriod(time::system_clock::now() - time::days(100),
                                                                                        time::system_clock::now() - time::seconds(1))),
                                        schema));
}

BOOST_AUTO_TEST_CASE(TestSignerVerifierSelfBadCert2)
{
  MpsSigner signer("/a/b/c/KEY/1234");
  auto pub = signer.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert(signer.getSignerKeyName(), pub);

  MultipartySchema schema;
  schema.signers.emplace_back(signer.getSignerKeyName());

  auto cert = signer.getSelfSignCert(
      security::ValidityPeriod(time::system_clock::now() - time::seconds(1),
                               time::system_clock::now() + time::days(100)));
  auto newBuffer = make_shared<Buffer>(cert.getContent().value(), cert.getContent().value_size() - 1);
  cert.setContent(newBuffer);

  BOOST_CHECK(!verifier.verifySignature(cert,
                                        schema));

  MpsVerifier loader;
  BOOST_CHECK_THROW(loader.addCert(cert), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(TestAggregateSignVerify)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), WildCardName("/a/b/c"));
  auto pub = signer.getPublicKey();

  MpsSigner signer2("/a/b/d");
  auto pub2 = signer2.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert("/a/b/c", pub);
  verifier.addCert("/a/b/d", pub2);

  Data data1;
  data1.setName(Name("/a/b/c/d"));
  data1.setContent(Name("/1/2/3/4").wireEncode());

  MultipartySchema schema;
  schema.signers.emplace_back(WildCardName(signer.getSignerKeyName()));
  schema.signers.emplace_back(WildCardName(signer2.getSignerKeyName()));

  //add signer list
  SignatureInfo info(static_cast<ndn::tlv::SignatureTypeValue>(tlv::SignatureSha256WithBls), KeyLocator("/some/signer/list"));
  data1.setSignatureInfo(info);
  MpsSignerList list;
  list.emplace_back(signer.getSignerKeyName());
  list.emplace_back(signer2.getSignerKeyName());
  verifier.addSignerList("/some/signer/list", list);

  //sign
  auto sig1 = signer.getSignature(data1);
  auto sig2 = signer2.getSignature(data1);
  BOOST_ASSERT(verifier.verifySignaturePiece(data1, signer.getSignerKeyName(), sig1));
  BOOST_ASSERT(verifier.verifySignaturePiece(data1, signer2.getSignerKeyName(), sig2));

  MpsAggregator aggregater;
  std::vector<blsSignature> signatures;
  {
    blsSignature sig1s;
    BOOST_ASSERT(blsSignatureDeserialize(&sig1s, sig1.value(), sig1.value_size()));
    signatures.emplace_back(sig1s);
  }
  {
    blsSignature sig2s;
    BOOST_ASSERT(blsSignatureDeserialize(&sig2s, sig2.value(), sig2.value_size()));
    signatures.emplace_back(sig2s);
  }
  aggregater.buildMultiSignature(data1, signatures);
  BOOST_ASSERT(verifier.verifySignature(data1, schema));
}

BOOST_AUTO_TEST_CASE(TestAggregateSignVerifyBadKey)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), WildCardName("/a/b/c"));
  auto pub = signer.getPublicKey();

  MpsSigner signer2("/a/b/d");
  auto pub2 = signer2.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert("/a/b/c", pub);
  verifier.addCert("/a/b/d", pub2);

  Data data1;
  data1.setName(Name("/a/b/c/d"));
  data1.setContent(Name("/1/2/3/4").wireEncode());

  MultipartySchema schema;
  schema.signers.emplace_back(WildCardName(signer.getSignerKeyName()));
  schema.signers.emplace_back(WildCardName(signer2.getSignerKeyName()));

  //add signer list
  SignatureInfo info(static_cast<ndn::tlv::SignatureTypeValue>(tlv::SignatureSha256WithBls), KeyLocator("/some/signer/list"));
  data1.setSignatureInfo(info);
  MpsSignerList list;
  list.emplace_back(signer.getSignerKeyName());
  list.emplace_back(signer.getSignerKeyName());  //wrong!
  verifier.addSignerList("/some/signer/list", list);

  //sign
  auto sig1 = signer.getSignature(data1);
  auto sig2 = signer2.getSignature(data1);
  BOOST_ASSERT(verifier.verifySignaturePiece(data1, signer.getSignerKeyName(), sig1));
  BOOST_ASSERT(verifier.verifySignaturePiece(data1, signer2.getSignerKeyName(), sig2));

  MpsAggregator aggregater;
  std::vector<blsSignature> signatures;
  {
    blsSignature sig1s;
    BOOST_ASSERT(blsSignatureDeserialize(&sig1s, sig1.value(), sig1.value_size()));
    signatures.emplace_back(sig1s);
  }
  {
    blsSignature sig2s;
    BOOST_ASSERT(blsSignatureDeserialize(&sig2s, sig2.value(), sig2.value_size()));
    signatures.emplace_back(sig2s);
  }
  aggregater.buildMultiSignature(data1, signatures);
  BOOST_ASSERT(!verifier.verifySignature(data1, schema));
}

BOOST_AUTO_TEST_CASE(TestAggregateSignVerifyBadSig)
{
  MpsSigner signer("/a/b/c");
  BOOST_CHECK_EQUAL(signer.getSignerKeyName(), WildCardName("/a/b/c"));
  auto pub = signer.getPublicKey();

  MpsSigner signer2("/a/b/d");
  auto pub2 = signer2.getPublicKey();

  MpsVerifier verifier;
  verifier.addCert("/a/b/c", pub);
  verifier.addCert("/a/b/d", pub2);

  Data data1;
  data1.setName(Name("/a/b/c/d"));
  data1.setContent(Name("/1/2/3/4").wireEncode());

  MultipartySchema schema;
  schema.signers.emplace_back(WildCardName(signer.getSignerKeyName()));
  schema.signers.emplace_back(WildCardName(signer2.getSignerKeyName()));

  //add signer list
  SignatureInfo info(static_cast<ndn::tlv::SignatureTypeValue>(tlv::SignatureSha256WithBls), KeyLocator("/some/signer/list"));
  data1.setSignatureInfo(info);
  MpsSignerList list;
  list.emplace_back(signer.getSignerKeyName());
  list.emplace_back(signer2.getSignerKeyName());
  verifier.addSignerList("/some/signer/list", list);

  //sign
  auto sig1 = signer.getSignature(data1);
  BOOST_ASSERT(verifier.verifySignaturePiece(data1, signer.getSignerKeyName(), sig1));
  auto sig2 = signer2.getSignature(data1);
  data1.setContent(Name("/1/2/3/4/5").wireEncode());
  BOOST_ASSERT(!verifier.verifySignaturePiece(data1, signer2.getSignerKeyName(), sig2));

  MpsAggregator aggregater;
  std::vector<blsSignature> signatures;
  {
    blsSignature sig1s;
    BOOST_ASSERT(blsSignatureDeserialize(&sig1s, sig1.value(), sig1.value_size()));
    signatures.emplace_back(sig1s);
  }
  {
    blsSignature sig2s;
    BOOST_ASSERT(blsSignatureDeserialize(&sig2s, sig2.value(), sig2.value_size()));
    signatures.emplace_back(sig2s);
  }
  aggregater.buildMultiSignature(data1, signatures);
  BOOST_ASSERT(!verifier.verifySignature(data1, schema));
}

BOOST_AUTO_TEST_SUITE_END()  // TestCryptoPlayers

}  // namespace tests
}  // namespace mps
}  // namespace ndn
