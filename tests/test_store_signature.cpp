#include <QTest>

#include <QJsonArray>
#include <QJsonObject>

#include "engine/store-signature.h"

using namespace kai::engine::store_signature;

class TestStoreSignature : public QObject {
    Q_OBJECT

private slots:
    // RFC 8032 §7.1, teste 1 (mensagem vazia).
    void rfc8032Vector1()
    {
        const QByteArray seed = QByteArray::fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
        const QByteArray pub = QByteArray::fromHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
        const QByteArray sig = QByteArray::fromHex(
            "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
        QCOMPARE(publicKeyFromSecret(seed), pub);
        QCOMPARE(sign(QByteArray(), seed), sig);
        QVERIFY(verify(QByteArray(), sig, pub));
    }

    // RFC 8032 §7.1, teste 2 (um byte).
    void rfc8032Vector2()
    {
        const QByteArray seed = QByteArray::fromHex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
        const QByteArray pub = QByteArray::fromHex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
        const QByteArray msg = QByteArray::fromHex("72");
        const QByteArray sig = QByteArray::fromHex(
            "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
        QCOMPARE(publicKeyFromSecret(seed), pub);
        QCOMPARE(sign(msg, seed), sig);
        QVERIFY(verify(msg, sig, pub));
    }

    void tamperedMessageOrWrongKeyFails()
    {
        const KeyPair a = generateKeyPair();
        const KeyPair b = generateKeyPair();
        const QByteArray sig = sign("hello", a.secretKey);
        QVERIFY(verify("hello", sig, a.publicKey));
        QVERIFY(!verify("hellp", sig, a.publicKey));
        QVERIFY(!verify("hello", sig, b.publicKey));
        QByteArray bad = sig;
        bad[10] = static_cast<char>(bad[10] ^ 1);
        QVERIFY(!verify("hello", bad, a.publicKey));
    }

    void indexVerificationStates()
    {
        const KeyPair kp = generateKeyPair();
        QJsonObject index{{"store_format", 1}, {"packages", QJsonArray()}};
        QCOMPARE(verifyIndex(index, QString()), Result::NoKeyDeclared);
        QCOMPARE(verifyIndex(index, toBase64(kp.publicKey)), Result::Missing);
        index["signature"] = toBase64(sign(canonicalBody(index), kp.secretKey));
        QCOMPARE(verifyIndex(index, toBase64(kp.publicKey)), Result::Ok);
        index["generated"] = QStringLiteral("tampered");
        QCOMPARE(verifyIndex(index, toBase64(kp.publicKey)), Result::Invalid);
    }
};

QTEST_MAIN(TestStoreSignature)
#include "test_store_signature.moc"
