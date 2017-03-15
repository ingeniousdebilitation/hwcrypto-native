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

#include "sign.h"

#include "pcsc.h"
#include "modulemap.h"
#include "qt_signer.h"
#include "qt_certselect.h"
#include "util.h"
#include "Logger.h"

#ifdef _WIN32
#include "WinCertSelect.h"
#include "WinSigner.h"
#endif


QVariantMap Sign::sign(QtHost *h, const QJsonObject &json) {

    if (!json.contains("cert") || !json.contains("hash")) {
        return {{"result", "invalid_argument"}};
    }
    // TODO: If parameter is not the same, reject operation

    std::vector<unsigned char> hash = ba2v(QByteArray::fromBase64(json.value("hash").toString().toLatin1()));
    _log("Signing: %s", toHex(hash).c_str());
    std::vector<unsigned char> signature;
#ifdef _WIN32
    if (h->winsign) {
        signature = WinSigner::sign(hash, h->signcert);
    }
#endif
    if (signature.empty())
        signature = QtSigner::sign(h->pkcs11, hash, h->signcert, h->friendly_origin, Signing);
    return {{"signature", v2ba(signature).toBase64()}};
}

// Show a certificate selctor and save the certificate
QVariantMap Sign::select(QtHost *h, const QJsonObject &json) {
    (void)json;

    // FIXME:: pkcs11 get signing certs
    // Load the first module FIXME
    std::vector<std::vector<unsigned char>> atrs = PCSC::atrList(false);
    std::vector<std::string> modules = P11Modules::getPaths(atrs);

    std::vector<unsigned char> cert;

    if (modules.empty()) {
#ifdef _WIN32
        // No PKCS#11 modules detected for any of the connected cards.
        // Check if we can find a cert from certstore
        cert = WinCertSelect::getCert(CertificatePurpose::Signing, LPWSTR(tr("Signing on %1, please select certificate").arg(h->friendly_origin).utf16()));
        if (!cert.empty()) {
            h->signcert = cert;
            h->winsign = true;
            return {{"cert", v2ba(cert).toBase64()}};
        }
#endif
        // If not modules, we have an error
        return {{"result", "no_certificates"}};
    } else {
        h->pkcs11.load(modules[0]);
        std::vector<std::vector<unsigned char>> certs = h->pkcs11.getSignCerts();
        if (certs.empty()) {
            return {{"result", "no_certificates"}};
        } else  {
            cert = QtCertSelect::getCert(certs, h->friendly_origin, Signing);
            h->signcert = cert;
            return {{"result", "ok"}, {"cert", v2ba(cert).toBase64() }};
        }
    }
    // never reachable
    throw std::runtime_error("Should not be reachable");
}
