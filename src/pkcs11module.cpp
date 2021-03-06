/*
 * Chrome Token Signing Native Host
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include "pkcs11module.h"
#include "Logger.h"
#include "util.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <stdexcept>
#include <iostream>
#include <map>
#include <vector>

#include <QSslCertificate>
#include <QSslCertificateExtension>
#include <QByteArray>
#include <QJsonDocument>
#include <QMetaType>
#include <QList>

#ifndef _WIN32
#include <dlfcn.h>
#endif

// TODO: update to PKCS#11 v2.30 header
#define CKR_LIBRARY_LOAD_FAILED               0x000001B7

// Wrapper around a single PKCS#11 module
template <typename Func, typename... Args>
CK_RV Call(const char *fun, const char *file, int line, const char *function, Func func, Args... args)
{
    CK_RV rv = func(args...);
    Logger::writeLog(fun, file, line, "%s: %s", function, PKCS11Module::errorName(rv));
    return rv;
}
#define C(API, ...) Call(__FUNCTION__, __FILE__, __LINE__, "C_"#API, fl->C_##API, __VA_ARGS__)

// return the rv is not CKR_OK
#define check_C(API, ...) do { \
    CK_RV _ret = Call(__FUNCTION__, __FILE__, __LINE__, "C_"#API, fl->C_##API, __VA_ARGS__); \
    if (_ret != CKR_OK) { \
       Logger::writeLog(__FUNCTION__, __FILE__, __LINE__, "returning %s", PKCS11Module::errorName(_ret)); \
       return _ret; \
    } \
} while(0)

std::vector<unsigned char> PKCS11Module::attribute(CK_ATTRIBUTE_TYPE type, CK_SESSION_HANDLE sid, CK_OBJECT_HANDLE obj) const
{
    CK_ATTRIBUTE attr = {type, nullptr, 0};
    C(GetAttributeValue, sid, obj, &attr, 1UL);
    std::vector<unsigned char> data(attr.ulValueLen, 0);
    attr.pValue = data.data();
    C(GetAttributeValue, sid, obj, &attr, 1UL);
    return data;
}

std::vector<CK_OBJECT_HANDLE> PKCS11Module::objects(CK_OBJECT_CLASS objectClass, CK_SESSION_HANDLE session, CK_ULONG count) const
{
    return objects({ {CKA_CLASS, &objectClass, sizeof(objectClass)} }, session, count);
}

std::vector<CK_OBJECT_HANDLE> PKCS11Module::objects(const std::vector<CK_ATTRIBUTE> &attr, CK_SESSION_HANDLE session, CK_ULONG count) const
{
    C(FindObjectsInit, session, const_cast<CK_ATTRIBUTE*>(attr.data()), CK_ULONG(attr.size()));
    CK_ULONG objectCount = count;
    std::vector<CK_OBJECT_HANDLE> objects(objectCount);
    C(FindObjects, session, objects.data(), objects.size(), &objectCount);
    C(FindObjectsFinal, session);
    objects.resize(objectCount);
    return objects;
}

std::vector<CK_OBJECT_HANDLE> PKCS11Module::getKey(CK_SESSION_HANDLE session, const std::vector<unsigned char> &id) const {
    _log("Looking for key with id %s length %d", toHex(id).c_str(), id.size());
    CK_OBJECT_CLASS keyclass = CKO_PRIVATE_KEY;
    return objects({
        {CKA_CLASS, &keyclass, sizeof(keyclass)},
        {CKA_ID, (void*)id.data(), id.size()}
    }, session, 1);
}


// TODO: move this to QtPKI
static bool usage_matches(const std::vector<unsigned char> &certificateCandidate, CertificatePurpose type)
{
    QSslCertificate cert = v2cert(certificateCandidate);
    bool isCa = true;
    bool isSSLClient = false;
    bool isNonRepudiation = false;

    for (const QSslCertificateExtension &ext: cert.extensions()) {
        QVariant v = ext.value();
//      _log("ext: %s", ext.name().toStdString().c_str());
        if (ext.name() == "basicConstraints") {
            QVariantMap m = ext.value().toMap();
            isCa = m.value("ca").toBool();
        } else if (ext.oid() == "2.5.29.37") {
            // 2.5.29.37 - extendedKeyUsage
            // XXX: these are not declared stable by Qt.
            // Linux returns parsed map (OpenSSL?) OSX 5.8 returns QByteArrays, 5.5 works
            if (v.canConvert<QByteArray>()) {
                // XXX: this is 06082b06010505070302 what is 1.3.6.1.5.5.7.3.2 what is "TLS Client"
                if (v.toByteArray().toHex().contains("06082b06010505070302")) {
                    isSSLClient = true;
                }
            } else if (v.canConvert<QList<QVariant>>()) {
                // Linux
                if (v.toList().contains("TLS Web Client Authentication")) {
                    isSSLClient = true;
                }
            }
        } else if (ext.name() == "keyUsage") {
            if (v.canConvert<QList<QVariant>>()) {
                // Linux
                if (v.toList().contains("Non Repudiation")) {
                    isNonRepudiation = true;
                }
            }
            // FIXME: detect NR from byte array
            // Do a ugly trick for esteid only
            QList<QString> ou = cert.subjectInfo(QSslCertificate::OrganizationalUnitName);
            if (!isNonRepudiation && ou.size() > 0 && ou.at(0) == "digital signature") {
                isNonRepudiation = true;
            }
            _log("keyusage: %s", v.toByteArray().toHex().toStdString().c_str());
        }
    }
    _log("Certificate flags: ca=%d auth=%d nonrepu=%d", isCa, isSSLClient, isNonRepudiation);
    return !isCa &&
           ((type & Authentication && isSSLClient) || (type & Signing && isNonRepudiation));
}


CK_RV PKCS11Module::load(const std::string &module) {
    // Clear any present modules
    certs.clear();
    CK_C_GetFunctionList C_GetFunctionList = nullptr;
#ifdef _WIN32
    library = LoadLibraryA(module.c_str());
    if (library)
        C_GetFunctionList = CK_C_GetFunctionList(GetProcAddress(library, "C_GetFunctionList"));
#else
    library = dlopen(module.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (library) {
#ifdef __linux__
        // Get path to library location, if just a name
        if (path.find_first_of("/\\") == std::string::npos) {
            std::vector<char> path(1024, 0);
            if (dlinfo(library,  RTLD_DI_ORIGIN, path.data()) == 0) {
                std::string p(path.begin(), path.end());
                _log("Loaded %s from %s", module.c_str(), p.c_str());
            } else {
                _log("Warning: could not get library load path");
            }
        }
#endif
        C_GetFunctionList = CK_C_GetFunctionList(dlsym(library, "C_GetFunctionList"));
    }
#endif

    if (!C_GetFunctionList) {
        _log("Module does not have C_GetFunctionList");
        return CKR_LIBRARY_LOAD_FAILED; // XXX Not really what we had in mind according to spec spec, but usable.
    }
    Call(__FUNCTION__, __FILE__, __LINE__, "C_GetFunctionList", C_GetFunctionList, &fl);
    CK_RV rv = C(Initialize, nullptr);
    if (rv != CKR_OK && rv != CKR_CRYPTOKI_ALREADY_INITIALIZED) {
        return rv;
    } else {
        initialized = rv != CKR_CRYPTOKI_ALREADY_INITIALIZED;
    }

    // Locate all slots with tokens
    std::vector<CK_SLOT_ID> slots_with_tokens;
    CK_ULONG slotCount = 0;
    check_C(GetSlotList, CK_TRUE, nullptr, &slotCount);
    _log("slotCount = %i", slotCount);
    slots_with_tokens.resize(slotCount);
    check_C(GetSlotList, CK_TRUE, slots_with_tokens.data(), &slotCount);
    for (auto slot: slots_with_tokens) {
        // Check the content of the slot
        CK_TOKEN_INFO token;
        CK_SESSION_HANDLE sid = 0;
        _log("Checking slot %u", slot);
        rv = C(GetTokenInfo, slot, &token);
        if (rv != CKR_OK) {
            _log("Could not get token info, skipping slot %u", slot);
            continue;
        }
        std::string label = QString::fromUtf8((const char* )token.label, sizeof(token.label)).simplified().toStdString();
        _log("Token has a label: \"%s\"", label.c_str());
        C(OpenSession, slot, CKF_SERIAL_SESSION, nullptr, nullptr, &sid);
        if (rv != CKR_OK) {
            _log("Could not open session, skipping slot %u", slot);
            continue;
        }
        _log("Opened session: %u", sid);
        // CK_OBJECT_CLASS objectClass
        std::vector<CK_OBJECT_HANDLE> objectHandle = objects(CKO_CERTIFICATE, sid, 2);
        // We now have the certificate handles (valid for this session) in objectHandle
        _log("Found %u certificates from slot %u", objectHandle.size(), slot);
        for (CK_OBJECT_HANDLE handle: objectHandle) {
            // Get DER
            std::vector<unsigned char> certCandidate = attribute(CKA_VALUE, sid, handle);
            // Get certificate ID
            std::vector<unsigned char> certid = attribute(CKA_ID, sid, handle);
            _log("Found certificate: %s %s", x509subject(certCandidate).c_str(), toHex(certid).c_str());
            // add to map
            certs[certCandidate] = std::make_pair(P11Token({(int)token.ulMinPinLen, (int)token.ulMaxPinLen, label, (bool)(token.flags & CKF_PROTECTED_AUTHENTICATION_PATH), slot, token.flags}), certid);
        }
        // Close session with this slot. We ignore errors here
        C(CloseSession, sid);
    }
    // List all found certs
    _log("found %d certificates", certs.size());
    for(const auto &cpairs : certs) {
        auto location = cpairs.second;
        _log("certificate: %s in slot %d with id %s", x509subject(cpairs.first).c_str(), location.first.slot, toHex(location.second).c_str());
    }
    return CKR_OK;
}

std::vector<std::vector <unsigned char>> PKCS11Module::getCerts(CertificatePurpose type) {
    std::vector<std::vector<unsigned char>> res;
    for(auto const &crts: certs) {
        _log("certificate: %s", toHex(crts.first).c_str());
        if(usage_matches(crts.first, type))
            res.push_back(crts.first);
    }
    return res;
}

CK_RV PKCS11Module::login(const std::vector<unsigned char> &cert, const char *pin) {
    _log("Issuing C_Login");
    auto slot = certs.find(cert)->second; // FIXME: not found

    // Assumes presence of session
    if (!session) {
        const P11Token *token = getP11Token(cert);
        if (!token) {
            // FIXME: error
            return CKR_TOKEN_NOT_PRESENT;
        }
        _log("Using key from slot %d with ID %s", slot.first.slot, toHex(slot.second).c_str());
        check_C(OpenSession, token->slot, CKF_SERIAL_SESSION, nullptr, nullptr, &session);
    }
    check_C(Login, session, CKU_USER, (unsigned char*)pin, pin ? strlen(pin) : 0);

    return CKR_OK;
}



PKCS11Module::~PKCS11Module() {
    if (session)
        C(CloseSession, session);
    if (initialized)
        C(Finalize, nullptr);
    if (!library)
        return;
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
}

CK_RV PKCS11Module::sign(const std::vector<unsigned char> &cert, const std::vector<unsigned char> &hash, std::vector<unsigned char> &result) {
    auto slot = certs.find(cert)->second; // FIXME: not found

    // Assumes open session to the right token, that is already authenticated.
    // TODO: correct handling of CKA_ALWAYS_AUTHENTICATE and associated login procedures
    std::vector<CK_OBJECT_HANDLE> key = getKey(session, slot.second); // TODO: function signature and return code
    if (key.size() != 1) {
        _log("Can not sign - no key or found multiple matches");
        return CKR_OBJECT_HANDLE_INVALID;
    }

    CK_MECHANISM mechanism = {CKM_RSA_PKCS, 0, 0};
    check_C(SignInit, session, &mechanism, key[0]);
    std::vector<unsigned char> hashWithPadding;
    // FIXME: explicit hash type argument
    switch (hash.size()) {
    case BINARY_SHA224_LENGTH:
        hashWithPadding = {0x30, 0x2d, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04, 0x05, 0x00, 0x04, 0x1c};
        break;
    case BINARY_SHA256_LENGTH:
        hashWithPadding = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
        break;
    case BINARY_SHA384_LENGTH:
        hashWithPadding = {0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};
        break;
    case BINARY_SHA512_LENGTH:
        hashWithPadding = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};
        break;
    default:
        _log("incorrect digest length, dropping padding");
    }
    hashWithPadding.insert(hashWithPadding.end(), hash.begin(), hash.end());
    CK_ULONG signatureLength = 0;

    // Get response size
    check_C(Sign, session, hashWithPadding.data(), hashWithPadding.size(), nullptr, &signatureLength);
    result.resize(signatureLength);
    // Get actual signature
    check_C(Sign, session, hashWithPadding.data(), hashWithPadding.size(), result.data(), &signatureLength);

    _log("Signature: %s", toHex(result).c_str());

    // We require a new login for next signature
    // return code is ignored
    C(CloseSession, session);
    session = CK_INVALID_HANDLE;
    return CKR_OK;
}

std::pair<int, int> PKCS11Module::getPINLengths(const std::vector<unsigned char> &cert) {
    const P11Token *token = getP11Token(cert);
    return std::make_pair(token->pin_min, token->pin_max);
}

bool PKCS11Module::isPinpad(const std::vector<unsigned char> &cert) const {
    return getP11Token(cert)->has_pinpad;
}

// FIXME: assumes 3 tries for a PIN
int PKCS11Module::getPINRetryCount(const P11Token &p11token) {
    if (p11token.flags & CKF_USER_PIN_LOCKED)
        return 0;
    if (p11token.flags & CKF_USER_PIN_FINAL_TRY)
        return 1;
    if (p11token.flags & CKF_USER_PIN_COUNT_LOW)
        return 2;
    return 3;
}

const P11Token *PKCS11Module::getP11Token(const std::vector<unsigned char> &cert) const {
    auto slotinfo = certs.find(cert);
    if (slotinfo == certs.end()) {
        return nullptr;
    }
    return &(slotinfo->second.first);
}
const char *PKCS11Module::errorName(CK_RV err) {
    switch (err) {
    case CKR_OK:
        return "CKR_OK";
    case CKR_CANCEL:
        return "CKR_CANCEL";
    case CKR_HOST_MEMORY:
        return "CKR_HOST_MEMORY";
    case CKR_SLOT_ID_INVALID:
        return "CKR_SLOT_ID_INVALID";
    case CKR_GENERAL_ERROR:
        return "CKR_GENERAL_ERROR";
    case CKR_FUNCTION_FAILED:
        return "CKR_FUNCTION_FAILED";
    case CKR_ARGUMENTS_BAD:
        return "CKR_ARGUMENTS_BAD";
    case CKR_NO_EVENT:
        return "CKR_NO_EVENT";
    case CKR_NEED_TO_CREATE_THREADS:
        return "CKR_NEED_TO_CREATE_THREADS";
    case CKR_CANT_LOCK:
        return "CKR_CANT_LOCK";
    case CKR_ATTRIBUTE_READ_ONLY:
        return "CKR_ATTRIBUTE_READ_ONLY";
    case CKR_ATTRIBUTE_SENSITIVE:
        return "CKR_ATTRIBUTE_SENSITIVE";
    case CKR_ATTRIBUTE_TYPE_INVALID:
        return "CKR_ATTRIBUTE_TYPE_INVALID";
    case CKR_ATTRIBUTE_VALUE_INVALID:
        return "CKR_ATTRIBUTE_VALUE_INVALID";
    case CKR_DATA_INVALID:
        return "CKR_DATA_INVALID";
    case CKR_DATA_LEN_RANGE:
        return "CKR_DATA_LEN_RANGE";
    case CKR_DEVICE_ERROR:
        return "CKR_DEVICE_ERROR";
    case CKR_DEVICE_MEMORY:
        return "CKR_DEVICE_MEMORY";
    case CKR_DEVICE_REMOVED:
        return "CKR_DEVICE_REMOVED";
    case CKR_ENCRYPTED_DATA_INVALID:
        return "CKR_ENCRYPTED_DATA_INVALID";
    case CKR_ENCRYPTED_DATA_LEN_RANGE:
        return "CKR_ENCRYPTED_DATA_LEN_RANGE";
    case CKR_FUNCTION_CANCELED:
        return "CKR_FUNCTION_CANCELED";
    case CKR_FUNCTION_NOT_PARALLEL:
        return "CKR_FUNCTION_NOT_PARALLEL";
    case CKR_FUNCTION_NOT_SUPPORTED:
        return "CKR_FUNCTION_NOT_SUPPORTED";
    case CKR_KEY_HANDLE_INVALID:
        return "CKR_KEY_HANDLE_INVALID";
    case CKR_KEY_SIZE_RANGE:
        return "CKR_KEY_SIZE_RANGE";
    case CKR_KEY_TYPE_INCONSISTENT:
        return "CKR_KEY_TYPE_INCONSISTENT";
    case CKR_KEY_NOT_NEEDED:
        return "CKR_KEY_NOT_NEEDED";
    case CKR_KEY_CHANGED:
        return "CKR_KEY_CHANGED";
    case CKR_KEY_NEEDED:
        return "CKR_KEY_NEEDED";
    case CKR_KEY_INDIGESTIBLE:
        return "CKR_KEY_INDIGESTIBLE";
    case CKR_KEY_FUNCTION_NOT_PERMITTED:
        return "CKR_KEY_FUNCTION_NOT_PERMITTED";
    case CKR_KEY_NOT_WRAPPABLE:
        return "CKR_KEY_NOT_WRAPPABLE";
    case CKR_KEY_UNEXTRACTABLE:
        return "CKR_KEY_UNEXTRACTABLE";
    case CKR_MECHANISM_INVALID:
        return "CKR_MECHANISM_INVALID";
    case CKR_MECHANISM_PARAM_INVALID:
        return "CKR_MECHANISM_PARAM_INVALID";
    case CKR_OBJECT_HANDLE_INVALID:
        return "CKR_OBJECT_HANDLE_INVALID";
    case CKR_OPERATION_ACTIVE:
        return "CKR_OPERATION_ACTIVE";
    case CKR_OPERATION_NOT_INITIALIZED:
        return "CKR_OPERATION_NOT_INITIALIZED";
    case CKR_PIN_INCORRECT:
        return "CKR_PIN_INCORRECT";
    case CKR_PIN_INVALID:
        return "CKR_PIN_INVALID";
    case CKR_PIN_LEN_RANGE:
        return "CKR_PIN_LEN_RANGE";
    case CKR_PIN_EXPIRED:
        return "CKR_PIN_EXPIRED";
    case CKR_PIN_LOCKED:
        return "CKR_PIN_LOCKED";
    case CKR_SESSION_CLOSED:
        return "CKR_SESSION_CLOSED";
    case CKR_SESSION_COUNT:
        return "CKR_SESSION_COUNT";
    case CKR_SESSION_HANDLE_INVALID:
        return "CKR_SESSION_HANDLE_INVALID";
    case CKR_SESSION_PARALLEL_NOT_SUPPORTED:
        return "CKR_SESSION_PARALLEL_NOT_SUPPORTED";
    case CKR_SESSION_READ_ONLY:
        return "CKR_SESSION_READ_ONLY";
    case CKR_SESSION_EXISTS:
        return "CKR_SESSION_EXISTS";
    case CKR_SESSION_READ_ONLY_EXISTS:
        return "CKR_SESSION_READ_ONLY_EXISTS";
    case CKR_SESSION_READ_WRITE_SO_EXISTS:
        return "CKR_SESSION_READ_WRITE_SO_EXISTS";
    case CKR_SIGNATURE_INVALID:
        return "CKR_SIGNATURE_INVALID";
    case CKR_SIGNATURE_LEN_RANGE:
        return "CKR_SIGNATURE_LEN_RANGE";
    case CKR_TEMPLATE_INCOMPLETE:
        return "CKR_TEMPLATE_INCOMPLETE";
    case CKR_TEMPLATE_INCONSISTENT:
        return "CKR_TEMPLATE_INCONSISTENT";
    case CKR_TOKEN_NOT_PRESENT:
        return "CKR_TOKEN_NOT_PRESENT";
    case CKR_TOKEN_NOT_RECOGNIZED:
        return "CKR_TOKEN_NOT_RECOGNIZED";
    case CKR_TOKEN_WRITE_PROTECTED:
        return "CKR_TOKEN_WRITE_PROTECTED";
    case CKR_UNWRAPPING_KEY_HANDLE_INVALID:
        return "CKR_UNWRAPPING_KEY_HANDLE_INVALID";
    case CKR_UNWRAPPING_KEY_SIZE_RANGE:
        return "CKR_UNWRAPPING_KEY_SIZE_RANGE";
    case CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT:
        return "CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT";
    case CKR_USER_ALREADY_LOGGED_IN:
        return "CKR_USER_ALREADY_LOGGED_IN";
    case CKR_USER_NOT_LOGGED_IN:
        return "CKR_USER_NOT_LOGGED_IN";
    case CKR_USER_PIN_NOT_INITIALIZED:
        return "CKR_USER_PIN_NOT_INITIALIZED";
    case CKR_USER_TYPE_INVALID:
        return "CKR_USER_TYPE_INVALID";
    case CKR_USER_ANOTHER_ALREADY_LOGGED_IN:
        return "CKR_USER_ANOTHER_ALREADY_LOGGED_IN";
    case CKR_USER_TOO_MANY_TYPES:
        return "CKR_USER_TOO_MANY_TYPES";
    case CKR_WRAPPED_KEY_INVALID:
        return "CKR_WRAPPED_KEY_INVALID";
    case CKR_WRAPPED_KEY_LEN_RANGE:
        return "CKR_WRAPPED_KEY_LEN_RANGE";
    case CKR_WRAPPING_KEY_HANDLE_INVALID:
        return "CKR_WRAPPING_KEY_HANDLE_INVALID";
    case CKR_WRAPPING_KEY_SIZE_RANGE:
        return "CKR_WRAPPING_KEY_SIZE_RANGE";
    case CKR_WRAPPING_KEY_TYPE_INCONSISTENT:
        return "CKR_WRAPPING_KEY_TYPE_INCONSISTENT";
    case CKR_RANDOM_SEED_NOT_SUPPORTED:
        return "CKR_RANDOM_SEED_NOT_SUPPORTED";
    case CKR_RANDOM_NO_RNG:
        return "CKR_RANDOM_NO_RNG";
    case CKR_DOMAIN_PARAMS_INVALID:
        return "CKR_DOMAIN_PARAMS_INVALID";
    case CKR_BUFFER_TOO_SMALL:
        return "CKR_BUFFER_TOO_SMALL";
    case CKR_SAVED_STATE_INVALID:
        return "CKR_SAVED_STATE_INVALID";
    case CKR_INFORMATION_SENSITIVE:
        return "CKR_INFORMATION_SENSITIVE";
    case CKR_STATE_UNSAVEABLE:
        return "CKR_STATE_UNSAVEABLE";
    case CKR_CRYPTOKI_NOT_INITIALIZED:
        return "CKR_CRYPTOKI_NOT_INITIALIZED";
    case CKR_CRYPTOKI_ALREADY_INITIALIZED:
        return "CKR_CRYPTOKI_ALREADY_INITIALIZED";
    case CKR_MUTEX_BAD:
        return "CKR_MUTEX_BAD";
    case CKR_MUTEX_NOT_LOCKED:
        return "CKR_MUTEX_NOT_LOCKED";
    case CKR_VENDOR_DEFINED:
        return "CKR_VENDOR_DEFINED";
    default:
        return "UNKNOWN";
    }
}