#include <set>
#include <utility>
#include "ndnmps/crypto-players.hpp"
#include "ndnmps/common.hpp"
#include "ndnmps/multi-party-signature.hpp"

namespace ndn {

static bool BLS_INITIALIZED = false;

void
bls_library_init() {
    if (!BLS_INITIALIZED) {
        int err = blsInit(MCL_BLS12_381, MCLBN_COMPILED_TIME_VAR);
        if (err != 0) {
            printf("blsInit err %d\n", err);
            exit(1);
        } else {
            BLS_INITIALIZED = true;
        }
    }
}

MpsSigner::MpsSigner(Name signerName)
{
    m_signerName = std::move(signerName);
    bls_library_init();
    blsSecretKeySetByCSPRNG(&m_sk);
    blsGetPublicKey(&m_pk, &m_sk);
}

MpsSigner::MpsSigner(Name signerName, Buffer secretKeyBuf)
{
    m_signerName = std::move(signerName);
    bls_library_init();
    auto ret = blsSecretKeyDeserialize(&m_sk, secretKeyBuf.data(), secretKeyBuf.size());
    if (ret == 0) {
        NDN_THROW(std::runtime_error("Fail to read secret key in Signer::initKey()"));
    }
    blsGetPublicKey(&m_pk, &m_sk);
}

Name
MpsSigner::getSignerKeyName() const
{
    return m_signerName;
}

blsPublicKey
MpsSigner::getPublicKey() const
{
    return m_pk;
}

blsSecretKey
MpsSigner::getSecretKey() const
{
    return m_sk;
}

std::vector<uint8_t>
MpsSigner::getpublicKeyStr() const
{
    std::vector<uint8_t> outputBuf(blsGetSerializedPublicKeyByteSize());
    int written_size = blsPublicKeySerialize(outputBuf.data(), outputBuf.size(), &m_pk);
    if (written_size == 0) {
        NDN_THROW(std::runtime_error("Fail to write public key in Signer::getpublicKeyStr()"));
    }
    outputBuf.resize(written_size);
    return std::move(outputBuf);
}

Block
MpsSigner::getSignature(Data data, const SignatureInfo& sigInfo) const
{
    if (sigInfo.getSignatureType() != tlv::SignatureSha256WithBls) {
        NDN_THROW(std::runtime_error("Signer got non-BLS signature type"));
    }

    data.setSignatureInfo(sigInfo);

    EncodingBuffer encoder;
    data.wireEncode(encoder, true);

    blsSignature sig;
    blsSign(&sig, &m_sk, encoder.buf(), encoder.size());
    auto signatureBuf = make_shared<Buffer>(blsGetSerializedSignatureByteSize());
    auto written_size = blsSignatureSerialize(signatureBuf->data(), signatureBuf->size(), &sig);
    if (written_size == 0) {
        NDN_THROW(std::runtime_error("Error on serializing signature"));
    }
    signatureBuf->resize(written_size);
    return Block(tlv::SignatureValue, signatureBuf);
}

void
MpsSigner::sign(Data& data) const
{
    SignatureInfo info(static_cast<tlv::SignatureTypeValue>(tlv::SignatureSha256WithBls), m_signerName);
    auto signature = getSignature(data, info);

    data.setSignatureInfo(info);
    data.setSignatureValue(signature.getBuffer());
}

MpsVerifier::MpsVerifier()
{
    bls_library_init();
}

void
MpsVerifier::addCert(const Name& keyName, blsPublicKey pk)
{
    m_certs.emplace(keyName, pk);
}

void
MpsVerifier::addSignerList(const Name& listName, MpsSignerList list)
{
    m_signList.emplace(listName, list);
}

bool
MpsVerifier::readyToVerify(const Data& data) const
{
    const auto& sigInfo = data.getSignatureInfo();
    if (sigInfo.hasKeyLocator() && sigInfo.getKeyLocator().getType() == tlv::Name) {
        if (m_certs.count(sigInfo.getKeyLocator().getName()) == 1) return true;
        if (m_signList.count(sigInfo.getKeyLocator().getName()) == 0) return false;
        const auto& item = m_signList.at(sigInfo.getKeyLocator().getName());
        for (const auto& signers : item.getSigners()) {
            if (m_certs.count(signers) == 0) return false;
        }
        return true;
    } else {
        return false;
    }
}

std::vector<Name>
MpsVerifier::itemsToFetch(const Data& data) const
{
    std::vector<Name> ans;
    const auto& sigInfo = data.getSignatureInfo();
    if (sigInfo.hasKeyLocator() && sigInfo.getKeyLocator().getType() == tlv::Name) {
        if (m_certs.count(sigInfo.getKeyLocator().getName()) == 1) return ans;
        if (m_signList.count(sigInfo.getKeyLocator().getName()) == 0) {
            ans.emplace_back(sigInfo.getKeyLocator().getName());
            return ans;
        }
        const auto& item = m_signList.at(sigInfo.getKeyLocator().getName());
        for (const auto& signers : item.getSigners()) {
            if (m_certs.count(signers) == 0) {
                ans.emplace_back(sigInfo.getKeyLocator().getName());
            }
        }
    }
    return ans;
}

bool
MpsVerifier::verifySignature(const Data& data, const MultipartySchema& schema) const
{
    auto sigInfo = data.getSignatureInfo();
    MpsSignerList locator;
    if (sigInfo.getKeyLocator().getType() == tlv::Name) {
        if (m_signList.count(sigInfo.getKeyLocator().getName()) != 0) {
            locator = m_signList.at(sigInfo.getKeyLocator().getName());
        } else if (m_certs.count(sigInfo.getKeyLocator().getName()) != 0) {
            locator.getSigners().emplace_back(sigInfo.getKeyLocator().getName());
        } else {
            return false;
        }
    }
    if (!schema.verifyKeyLocator(locator)) return false;

    //check signature
    std::vector<blsPublicKey> keys;
    for (const auto& signer: locator.getSigners()) {
        auto it = m_certs.find(signer);
        if (it == m_certs.end()) return false;
        keys.emplace_back(it->second);
    }

    //get signature value
    auto sigValue = data.getSignatureValue();
    blsSignature sig;
    if (blsSignatureDeserialize(&sig, sigValue.value(), sigValue.value_size()) == 0) return false;

    auto signedRanges = data.extractSignedRanges();
    if (signedRanges.size() == 1) { // to avoid copying in current ndn-cxx impl
        const auto& it = signedRanges.begin();
        return blsFastAggregateVerify(&sig, keys.data(), keys.size(), it->first, it->second);
    } else {
        EncodingBuffer encoder;
        for (const auto &it : signedRanges) {
            encoder.appendByteArray(it.first, it.second);
        }
        return blsFastAggregateVerify(&sig, keys.data(), keys.size(), encoder.buf(), encoder.size());
    }
}

bool
MpsVerifier::verifySignaturePiece(Data data, const SignatureInfo& sigInfo, const Name& signedBy, const Block& signaturePiece) const
{
    if (sigInfo.getSignatureType() != tlv::SignatureSha256WithBls) {
        NDN_THROW(std::runtime_error("Signer got non-BLS signature type"));
    }

    data.setSignatureInfo(sigInfo);

    blsPublicKey publicKey = m_certs.at(signedBy);
    blsSignature sig;
    if (blsSignatureDeserialize(&sig, signaturePiece.value(), signaturePiece.value_size()) == 0) return false;

    auto signedRanges = data.extractSignedRanges();
    if (signedRanges.size() == 1) { // to avoid copying in current ndn-cxx impl
        const auto& it = signedRanges.begin();
        return blsVerify(&sig, &publicKey, it->first, it->second);
    } else {
        EncodingBuffer encoder;
        for (const auto &it : signedRanges) {
            encoder.appendByteArray(it.first, it.second);
        }
        return blsVerify(&sig, &publicKey, encoder.buf(), encoder.size());
    }
}

MpsAggregater::MpsAggregater()
{
    bls_library_init();
}

void
MpsAggregater::buildMultiSignature(Data& data, const SignatureInfo& sigInfo,
        const std::vector<blsSignature>& collectedPiece) const
{

    data.setSignatureInfo(sigInfo);

    EncodingBuffer encoder;
    data.wireEncode(encoder, true);

    blsSignature outputSig;
    blsAggregateSignature(&outputSig, collectedPiece.data(), collectedPiece.size());
    auto sigBuffer = make_shared<Buffer>(blsGetSerializedSignatureByteSize());
    auto writtenSize = blsSignatureSerialize(sigBuffer->data(), sigBuffer->size(), &outputSig);
    if (writtenSize == 0) {
        NDN_THROW(std::runtime_error("Error on serializing"));
    }
    sigBuffer->resize(writtenSize);

    Block sigValue(tlv::SignatureValue,sigBuffer);

    data.wireEncode(encoder, sigValue);
}

optional<SignatureInfo>
MpsAggregater::getMinMPSignatureInfo(const MultipartySchema& schema, const std::vector<Name>& availableSingerKeys)
{
    auto result = schema.getMinSigners(availableSingerKeys);
    if (!result) return nullopt;
    else return MultiPartySignature::getMultiPartySignatureInfo(*result);
}

} // namespace ndn