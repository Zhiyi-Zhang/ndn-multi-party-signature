#include "ndnmps/players.hpp"

#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/util/logger.hpp>
#include <ndn-cxx/util/random.hpp>
#include <utility>

namespace ndn {
namespace mps {

NDN_LOG_INIT(ndnmps.players);

const time::milliseconds TIMEOUT = time::seconds(4);
const time::milliseconds ESTIMATE_PROCESS_TIME = time::seconds(1);
const static Name HMAC_KEY_PREFIX("/ndn/mps/hmac"); // append request ID when being used

void
onRegisterFail(const Name& prefix, const std::string& reason)
{
  NDN_LOG_ERROR("Fail to register prefix " << prefix.toUri() << " because " << reason);
}

Signer::Signer(std::unique_ptr<MpsSigner> mpsSigner, const Name& prefix, Face& face, const Name& keyName)
    : m_signer(std::move(mpsSigner))
    , m_prefix(prefix)
    , m_keyName(keyName)
    , m_face(face)
{
  // generate default key randomly
  m_sk.init();
  m_sk.getPublicKey(m_pk);

  Name invocationPrefix = m_prefix;
  invocationPrefix.append("mps").append("sign");
  m_handles.push_back(m_face.setInterestFilter(
      invocationPrefix, std::bind(&Signer::onSignRequest, this, _2), nullptr, onRegisterFail));

  Name resultPrefix = m_prefix;
  resultPrefix.append("mps").append("result");
  m_handles.push_back(m_face.setInterestFilter(
      resultPrefix, std::bind(&Signer::onResultFetch, this, _2), nullptr, onRegisterFail));
}

Signer::~Signer()
{
  for (auto& i : m_handles) {
    i.unregister();
  }
}

void
Signer::setDataVerifyCallback(const function<bool(const Data&)>& func)
{
  m_dataVerifyCallback = func;
}

void
Signer::setSignatureVerifyCallback(const function<bool(const Interest&)>& func)
{
  m_interestVerifyCallback = func;
}

void
Signer::onSignRequest(const Interest& interest)
{
  if (!m_interestVerifyCallback || !m_interestVerifyCallback(interest)) {
    m_face.put(generateAck(interest.getName(), ReplyCode::Unauthorized, 0));
    return;
  }
  // parse
  const auto& paramBlock = interest.getApplicationParameters();
  paramBlock.parse();
  Name wrapperName;
  try {
    if (paramBlock.get(tlv::ParameterDataName).isValid()) {
      wrapperName.wireDecode(paramBlock.get(tlv::ParameterDataName).blockFromValue());
      if (!wrapperName.at(-1).isImplicitSha256Digest()) {
        NDN_THROW(std::runtime_error("digest not found for data"));
      }
    }
    else {
      NDN_THROW(std::runtime_error("Block Element not found or Bad element type in signer's request"));
    }
    if (!interest.getName().get(m_prefix.size() + 2).isParametersSha256Digest()) {
      NDN_THROW(std::runtime_error("interest not end with parameter digest."));
    }
  }
  catch (const std::exception& e) {
    NDN_LOG_ERROR("Got error in decoding invocation request: " << e.what());
    m_face.put(generateAck(interest.getName(), ReplyCode::BadRequest, 0));
    return;
  }

  auto randomId = random::generateSecureWord64();
  RequestInstance instance;
  instance.code = ReplyCode::Processing;
  instance.version = 0;
  m_results.emplace(randomId, instance);
  m_face.put(generateAck(interest.getName(), ReplyCode::Processing, randomId));

  // fetch parameter
  Interest fetchInterest(wrapperName);
  fetchInterest.setCanBePrefix(false);
  fetchInterest.setMustBeFresh(true);
  fetchInterest.setInterestLifetime(TIMEOUT);
  m_face.expressInterest(
      fetchInterest,
      [&](auto& interest, auto& data) {
        // parse fetched data
        ReplyCode code = ReplyCode::OK;
        Data unsignedData;
        try {
          unsignedData.wireDecode(data.getContent().blockFromValue());
        }
        catch (const std::exception& e) {
          NDN_LOG_ERROR("Unsigned Data decoding error");
          code = ReplyCode::FailedDependency;
        }
        if (!m_dataVerifyCallback || !m_dataVerifyCallback(unsignedData)) {
          NDN_LOG_ERROR("Unsigned Data verification error");
          code = ReplyCode::Unauthorized;
        }
        // generate result
        m_results[randomId].code = code;
        if (code == ReplyCode::OK) {
          m_results[randomId].signatureValue = ndnGenBLSSignature(m_sk, unsignedData);
        }
      },
      [&](auto& interest, auto&) {
        // nack
        m_results[randomId].code = ReplyCode::FailedDependency;
      },
      [&](auto& interest) {
        // timeout
        m_results[randomId].code = ReplyCode::FailedDependency;
      });
}

void
Signer::onResultFetch(const Interest& interest)
{
  // parse request
  if (interest.getName().size() != m_prefix.size() + 3) {
    NDN_LOG_ERROR("Bad result request name format");
    // bad request let it timeout
    return;
  }
  auto resultId = readNonNegativeInteger(interest.getName().get(m_prefix.size() + 2));
  auto it = m_results.find(resultId);
  if (it == m_results.end()) {
    // replayed or phishing request, let it timeout
    return;
  }
  auto requestInstance = it->second;

  // otherwise, reply
  Data result(interest.getName());
  Block contentBlock(ndn::tlv::Content);
  contentBlock.push_back(makeStringBlock(tlv::Status, std::to_string(static_cast<int>(requestInstance.code))));
  if (requestInstance.code == ReplyCode::Processing) {
    requestInstance.version += 1;
    it->second = requestInstance;
    contentBlock.push_back(makeNonNegativeIntegerBlock(tlv::ResultAfter, ESTIMATE_PROCESS_TIME.count()));
    Name newResultName = m_prefix;
    newResultName.append("mps").append("result").appendNumber(resultId).appendVersion(requestInstance.version);
    contentBlock.push_back(makeNestedBlock(tlv::ResultName, newResultName));
  }
  else if (requestInstance.code == ReplyCode::OK) {
    m_results.erase(it);
    contentBlock.push_back(Block(ndn::tlv::SignatureValue, std::make_shared<Buffer>(requestInstance.signatureValue)));
  }
  else {
    m_results.erase(it);
  }
  contentBlock.encode();
  result.setContent(contentBlock);
  result.setFreshnessPeriod(TIMEOUT);
  ndnBLSSign(m_sk, result, m_keyName);
  m_face.put(result);
}

Data
Signer::generateAck(const Name& interestName, ReplyCode code, uint64_t requestId) const
{
  Data ack(interestName);
  Block contentBlock(ndn::tlv::Content);
  contentBlock.push_back(makeStringBlock(tlv::Status, std::to_string(static_cast<int>(ReplyCode::Processing))));
  if (code == ReplyCode::Processing) {
    contentBlock.push_back(makeNonNegativeIntegerBlock(tlv::ResultAfter, ESTIMATE_PROCESS_TIME.count()));
    Name newResultName = m_prefix;
    newResultName.append("mps").append("result").appendNumber(requestId);
    contentBlock.push_back(makeNestedBlock(tlv::ResultName, newResultName));
  }
  contentBlock.encode();
  ack.setContent(contentBlock);
  ack.setFreshnessPeriod(TIMEOUT);
  ndnBLSSign(m_sk, ack, m_keyName);
  return ack;
}

Verifier::Verifier(std::unique_ptr<MpsVerifier> verifier, Face& face, bool fetchKeys)
    : m_verifier(std::move(verifier))
    , m_face(face)
    , m_fetchKeys(fetchKeys)
{}

void
Verifier::setCertVerifyCallback(const function<bool(const Data&)>& func)
{
  m_certVerifyCallback = func;
}

void
Verifier::asyncVerifySignature(shared_ptr<const Data> data,
                               shared_ptr<const MultipartySchema> schema,
                               const VerifyFinishCallback& callback)
{
  uint32_t currentId = random::generateSecureWord32();
  if (m_verifier->readyToVerify(*data)) {
    callback(m_verifier->verifySignature(*data, *schema));
  }
  else {
    //store, fetch and wait
    VerificationRecord r{data, schema, callback, 0};
    for (const auto& item : m_verifier->itemsToFetch(*data)) {
      Interest interest(item);
      interest.setCanBePrefix(true);
      interest.setMustBeFresh(true);
      interest.setInterestLifetime(TIMEOUT);
      m_face.expressInterest(
          interest,
          std::bind(&Verifier::onData, this, _1, _2),
          std::bind(&Verifier::onNack, this, _1, _2),
          std::bind(&Verifier::onTimeout, this, _1));
      m_index[item].insert(currentId);
      r.itemLeft++;
    }
    m_queue.emplace(currentId, r);
  }
}

void
Verifier::removeAll(const Name& name)
{
  for (auto i : m_index[name]) {
    //remove them
    if (m_queue.count(i) != 0) {
      m_queue.at(i).callback(false);
      m_queue.erase(i);
    }
  }
  m_index.erase(name);
}

void
Verifier::onData(const Interest& interest, const Data& data)
{
  if (m_fetchKeys && security::Certificate::isValidName(data.getName())) {
    //certificate
    if (m_certVerifyCallback && m_certVerifyCallback(data)) {
      const auto& c = data.getContent();
      blsPublicKey key;
      int ret = blsPublicKeyDeserialize(&key, c.value(), c.value_size());
      if (ret == 0) {
        // decode failure
        removeAll(interest.getName());
        NDN_LOG_ERROR("Certificate cannot be decoded for " << interest.getName());
      }
      m_verifier->addCert(interest.getName(), key);
      satisfyItem(interest.getName());
    }
    else {
      removeAll(interest.getName());
      NDN_LOG_ERROR("Certificate cannot be verified for " << interest.getName());
    }
  }
  else {
    //signer list
    try {
      const auto& content = data.getContent();
      content.parse();
      if (content.get(tlv::MpsSignerList).isValid()) {
        m_verifier->addSignerList(interest.getName(), MpsSignerList(content.get(tlv::MpsSignerList)));
        satisfyItem(interest.getName());
        return;
      }
      else {
        removeAll(interest.getName());
        NDN_LOG_ERROR("signer list not found in " << interest.getName());
      }
    }
    catch (const std::exception& e) {
      NDN_LOG_ERROR("Catch error on decoding signer list packet: " << e.what());
    }
  }
}

void
Verifier::satisfyItem(const Name& itemName)
{
  for (auto i : m_index.at(itemName)) {
    if (m_queue.count(i) != 0) {
      if (m_queue.at(i).itemLeft == 1) {
        VerificationRecord r = m_queue.at(i);
        m_queue.erase(i);
        asyncVerifySignature(r.data, r.schema, r.callback);
      }
      else {
        m_queue.at(i).itemLeft--;
      }
    }
  }
  m_index.erase(itemName);
}

void
Verifier::onNack(const Interest& interest, const lp::Nack& nack)
{
  removeAll(interest.getName());
  NDN_LOG_ERROR("Received NACK with reason " << nack.getReason() << " for " << interest.getName());
}

void
Verifier::onTimeout(const Interest& interest)
{
  removeAll(interest.getName());
  NDN_LOG_ERROR("interest time out for " << interest.getName());
}

Initiator::Initiator(const MpsVerifier& verifier, const Name& prefix, Face& face, Scheduler& scheduler,
                     KeyChain& keyChain, const Name& signingKeyName)
    : m_verifier(verifier)
    , m_prefix(prefix)
    , m_face(face)
    , m_scheduler(scheduler)
    , m_signer(std::pair<KeyChain&, Name>(keyChain, signingKeyName))
{
  m_handle = m_face.setInterestFilter(
      m_prefix, std::bind(&Initiator::onWrapperFetch, this, _2), nullptr,
      Initiator::onRegisterFail);
}

Initiator::Initiator(const MpsVerifier& verifier, const Name& prefix, Face& face, Scheduler& scheduler,
                     const MpsSigner& dataSigner)
    : m_verifier(verifier)
    , m_prefix(prefix)
    , m_face(face)
    , m_scheduler(scheduler)
    , m_signer(dataSigner)
{
  m_handle = m_face.setInterestFilter(
      m_prefix, std::bind(&Initiator::onWrapperFetch, this, _2), nullptr,
      Initiator::onRegisterFail);
}

Initiator::InitiationRecord::InitiationRecord(const MultipartySchema& trySchema, std::shared_ptr<Data> data,
                                              const SignatureFinishCallback& successCb, const SignatureFailureCallback& failureCb)
    : schema(trySchema)
    , unsignedData(std::move(data))
    , onSuccess(successCb)
    , onFailure(failureCb) {}

Initiator::~Initiator()
{
  m_handle->unregister();
}
void
Initiator::addSigner(const Name& keyName, const Name& prefix)
{
  if (!m_verifier.getCerts().count(keyName)) {
    NDN_LOG_ERROR("do not know private key for" << keyName);
    NDN_THROW(std::runtime_error("do not know private key for" + keyName.toUri()));
  }
  m_keyToPrefix.emplace(keyName, prefix);
}

void
Initiator::addSigner(const Name& keyName, const blsPublicKey& keyValue, const Name& prefix)
{
  if (!m_verifier.getCerts().count(keyName)) {
    m_verifier.addCert(keyName, keyValue);
  }
  addSigner(keyName, prefix);
}

void
Initiator::multiPartySign(const MultipartySchema& schema, std::shared_ptr<Data> unfinishedData,
                          const SignatureFinishCallback& successCb, const SignatureFailureCallback& failureCb)
{
  //verify schema can be done
  std::vector<Name> keyToCheck;
  for (const auto& i : m_keyToPrefix) {
    if (!schema.getKeyMatches(i.first).empty()) {
      keyToCheck.emplace_back(i.first);
    }
  }
  if (schema.getMinSigners(keyToCheck).empty()) {
    NDN_LOG_WARN("Not enough available signers to satisfy schema");
    if (failureCb)
      failureCb("Not enough available signers to satisfy schema");
    return;
  }

  //register
  uint32_t currentId = random::generateSecureWord32();
  m_records.emplace(currentId, InitiationRecord(schema, std::move(unfinishedData), successCb, failureCb));
  auto& currentRecord = m_records.at(currentId);
  currentRecord.availableKeys = std::move(keyToCheck);

  //build signature info packet
  std::array<uint8_t, 8> wrapperBuf = {};
  random::generateSecureBytes(wrapperBuf.data(), 8);
  currentRecord.unsignedData->setSignatureInfo(
      SignatureInfo(static_cast<ndn::tlv::SignatureTypeValue>(tlv::SignatureSha256WithBls),
                    KeyLocator(Name(m_prefix).append("mps").append("signers").append(toHex(wrapperBuf.data(), 8)))));
  currentRecord.unsignedData->setSignatureValue(make_shared<Buffer>());  // placeholder

  //wrapper
  currentRecord.wrapper.setName(Name(m_prefix).append("mps").append("wrapper").append(toHex(wrapperBuf.data(), 8)));
  currentRecord.wrapper.setContent(makeNestedBlock(ndn::tlv::Content, *currentRecord.unsignedData));
  currentRecord.wrapper.setFreshnessPeriod(TIMEOUT);
  if (m_signer.index() == 0) {
    m_signer.get<0>().first.sign(currentRecord.wrapper, signingByKey(m_signer.get<0>().second));
  }
  else {
    m_signer.get<1>().sign(currentRecord.wrapper);
  }
  auto wrapperFullName = currentRecord.wrapper.getFullName();
  m_wrapToId.emplace(wrapperFullName, currentId);

  //send interest
  for (const Name& i : currentRecord.availableKeys) {
    Interest interest;
    interest.setName(Name(m_keyToPrefix.at(i)).append("mps").append("sign"));
    Block appParam(ndn::tlv::ApplicationParameters);
    appParam.push_back(makeNestedBlock(tlv::ParameterDataName, wrapperFullName));
    interest.setApplicationParameters(appParam);
    interest.setInterestLifetime(TIMEOUT);
    if (m_signer.index() == 0) {
      m_signer.get<0>().first.sign(interest, signingByKey(m_signer.get<0>().second));
    }
    else {
      m_signer.get<1>().sign(interest);
    }
    interest.setCanBePrefix(false);
    interest.setMustBeFresh(true);
    m_face.expressInterest(
        interest,
        std::bind(&Initiator::onData, this, currentId, i, _2),
        std::bind(&Initiator::onNack, this, currentId, i, _1, _2),
        std::bind(&Initiator::onTimeout, this, currentId, i, _1));
  }

  NDN_LOG_WARN("Sent all interest to initiate sign");
  currentRecord.eventId = m_scheduler.schedule(TIMEOUT + ESTIMATE_PROCESS_TIME + TIMEOUT,
                                               [&, currentId] { onSignTimeout(currentId); });
}

void
Initiator::onWrapperFetch(const Interest& interest)
{
  if (m_wrapToId.count(interest.getName())) {
    auto id = m_wrapToId.at(interest.getName());
    if (m_records.count(id)) {
      m_face.put(m_records.at(id).wrapper);
    }
    else {
      NDN_LOG_WARN("Unexpected wrapper " << interest);
      m_face.put(lp::Nack(interest));
    }
  }
  else {
    NDN_LOG_WARN("Unexpected wrapper " << interest);
    m_face.put(lp::Nack(interest));
  }
}

void
Initiator::onData(uint32_t id, const Name& keyName, const Data& data)
{
  if (m_records.count(id) == 0)
    return;
  const auto& content = data.getContent();
  content.parse();
  const auto& statusBlock = content.get(tlv::Status);
  if (!statusBlock.isValid()) {
    NDN_LOG_ERROR("Signer replied data with no status"
                  << "For interest " << data.getName());
    return;
  }
  ReplyCode status = static_cast<ReplyCode>(stoi(readString(statusBlock)));
  if (status == ReplyCode::Processing) {
    // schedule another pull
    time::milliseconds result_ms = ESTIMATE_PROCESS_TIME + ESTIMATE_PROCESS_TIME / 5;
    const auto& resultAfterBlock = content.get(tlv::ResultAfter);
    if (resultAfterBlock.isValid()) {
      result_ms = time::milliseconds(readNonNegativeInteger(resultAfterBlock));
    }
    const auto& resultAtBlock = content.get(tlv::ResultName);
    Name resultName;
    if (!resultAtBlock.isValid()) {
      NDN_LOG_ERROR("Signer processing but no result name replied: data for" << data.getName());
      keyLossTimeout(id, keyName);
      return;
    }
    else {
      try {
        resultName = Name(resultAtBlock.blockFromValue());
        if (data.getName().get(-1).isVersion()) {
          resultName.appendVersion(data.getName().get(-1).toVersion() + 1);
        }
      }
      catch (const std::exception& e) {
        NDN_LOG_ERROR("Signer processing but bad result name replied: data for" << data.getName());
        keyLossTimeout(id, keyName);
        return;
      }
    }
    m_scheduler.schedule(result_ms, [&, id, keyName, resultName] {
      Interest interest;
      interest.setName(resultName);
      interest.setCanBePrefix(true);
      interest.setMustBeFresh(true);
      interest.setInterestLifetime(TIMEOUT);
      m_face.expressInterest(
          interest,
          std::bind(&Initiator::onData, this, id, keyName, _2),
          std::bind(&Initiator::onNack, this, id, keyName, _1, _2),
          std::bind(&Initiator::onTimeout, this, id, keyName, _1));
    });
  }
  else if (status == ReplyCode::OK) {
    // add to record, may call success

    const Block& b = content.get(ndn::tlv::SignatureValue);
    if (!b.isValid()) {
      NDN_LOG_ERROR("Signer OK but bad signature value decoding failed: data for" << data.getName());
      keyLossTimeout(id, keyName);
      return;
    }
    blsSignature sig;
    int re = blsSignatureDeserialize(&sig, b.value(), b.value_size());
    if (re == 0) {
      NDN_LOG_ERROR("Signer OK but bad signature value decoding failed: data for" << data.getName());
      keyLossTimeout(id, keyName);
      return;
    }

    auto& record = m_records.at(id);
    if (!m_verifier.verifySignaturePiece(*record.unsignedData, keyName, b)) {
      // bad signature value
      NDN_LOG_ERROR("bad signature value from " << data.getName());
      keyLossTimeout(id, keyName);
      return;
    }
    record.signaturePieces.emplace(keyName, sig);
    std::vector<Name> successPiece(record.signaturePieces.size());
    for (const auto& i : record.signaturePieces) {
      successPiece.emplace_back(i.first);
    }
    if (!record.schema.getMinSigners(successPiece).empty()) {
      //success
      successCleanup(id);
    }
  }
  else {
    NDN_LOG_ERROR("Signer replied status: " << static_cast<int>(status) << "For interest " << data.getName());
    keyLossTimeout(id, keyName);
    return;
  }
}

void
Initiator::onNack(uint32_t id, const Name& keyName, const Interest& interest, const lp::Nack& nack)
{
  NDN_LOG_ERROR("NACK on interest " << interest.getName() << "For id " << id << " With reason " << nack.getReason());
  keyLossTimeout(id, keyName);
}

void
Initiator::onTimeout(uint32_t id, const Name& keyName, const Interest& interest)
{
  NDN_LOG_ERROR("Timeout on interest " << interest.getName() << "For id " << id);
  keyLossTimeout(id, keyName);
}

void
Initiator::onRegisterFail(const Name& prefix, const std::string& reason)
{
  NDN_LOG_ERROR("Fail to register prefix " << prefix.toUri() << " because " << reason);
}

void
Initiator::onSignTimeout(uint32_t id)
{
  if (m_records.count(id) == 0)
    return;
  auto record = m_records.at(id);
  std::vector<Name> successPiece(record.signaturePieces.size());
  for (const auto& i : record.signaturePieces) {
    successPiece.emplace_back(i.first);
  }
  if (!record.schema.getMinSigners(successPiece).empty()) {
    //success
    successCleanup(id);
  }
  else {
    //failure
    record.onFailure(std::string("Insufficient signature piece at timeout; collected ") +
                     std::to_string(successPiece.size()) + std::string(" Pieces"));
    NDN_LOG_ERROR("Insufficient signature piece at timeout; collected " << successPiece.size() << " Pieces");
  }

  m_wrapToId.erase(record.wrapper.getFullName());
  m_records.erase(id);
}

void
Initiator::successCleanup(uint32_t id)
{
  if (m_records.count(id) == 0)
    return;
  const auto& record = m_records.at(id);

  std::vector<Name> successPiece;
  std::vector<blsSignature> pieces(record.signaturePieces.size());
  for (const auto& i : record.signaturePieces) {
    successPiece.push_back(i.first);
    pieces.push_back(i.second);
  }

  MpsSignerList signerList(successPiece);
  Data signerListData;
  signerListData.setName(record.unsignedData->getSignatureInfo().getKeyLocator().getName());
  signerListData.setContent(signerList.wireEncode());
  signerListData.setFreshnessPeriod(record.unsignedData->getFreshnessPeriod());
  if (m_signer.index() == 0) {
    m_signer.get<0>().first.sign(signerListData, signingByKey(m_signer.get<0>().second));
  }
  else {
    m_signer.get<1>().sign(signerListData);
  }

  buildMultiSignature(*record.unsignedData, pieces);

  if (record.onSuccess) {
    record.onSuccess(record.unsignedData, std::move(signerListData));
  }

  record.eventId.cancel();

  m_wrapToId.erase(record.wrapper.getFullName());
  m_records.erase(id);
}

void
Initiator::keyLossTimeout(uint32_t id, const Name& keyName)
{
  if (m_records.count(id) == 0)
    return;
  auto& record = m_records.at(id);

  auto it = std::find(record.availableKeys.begin(), record.availableKeys.end(), keyName);
  if (it == record.availableKeys.end()) {
    return;
  }

  //erase it
  record.availableKeys.erase(it);
  if (record.schema.getMinSigners(record.availableKeys).empty()) {
    //failure
    record.onFailure(std::string("Too many signer refused to sign"));
    NDN_LOG_ERROR("Too many signer refused to sign " << record.unsignedData->getName());
    m_records.erase(id);
  }
}

}  // namespace mps
}  // namespace ndn