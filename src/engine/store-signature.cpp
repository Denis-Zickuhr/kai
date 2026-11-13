#include "engine/store-signature.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

namespace kai::engine::store_signature {

namespace {

// Porte do TweetNaCl (domínio público; Bernstein, Janssen, Lange, Schwabe, ...).
using i64 = std::int64_t;
using u64 = std::uint64_t;
using u8 = std::uint8_t;
using gf = i64[16];

const gf kGf0 = {0};
const gf kGf1 = {1};
const gf kD = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
               0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
const gf kD2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const gf kX = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
               0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const gf kY = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
               0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
const gf kI = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
               0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};

const u64 kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

void set25519(gf r, const gf a)
{
    for (int i = 0; i < 16; ++i) r[i] = a[i];
}

void car25519(gf o)
{
    for (int i = 0; i < 16; ++i) {
        o[i] += (1LL << 16);
        const i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * (1LL << 16);
    }
}

void sel25519(gf p, gf q, int b)
{
    const i64 c = ~(static_cast<i64>(b) - 1);
    for (int i = 0; i < 16; ++i) {
        const i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack25519(u8 *o, const gf n)
{
    gf m, t;
    for (int i = 0; i < 16; ++i) t[i] = n[i];
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = static_cast<int>((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = static_cast<u8>(t[i] & 0xff);
        o[2 * i + 1] = static_cast<u8>(t[i] >> 8);
    }
}

bool neq25519(const gf a, const gf b)
{
    u8 c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return std::memcmp(c, d, 32) != 0;
}

u8 par25519(const gf a)
{
    u8 d[32];
    pack25519(d, a);
    return d[0] & 1;
}

void unpack25519(gf o, const u8 *n)
{
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (static_cast<i64>(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

void A(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}

void Z(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}

void M(gf o, const gf a, const gf b)
{
    i64 t[31];
    for (int i = 0; i < 31; ++i) t[i] = 0;
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    car25519(o);
    car25519(o);
}

void S(gf o, const gf a)
{
    M(o, a, a);
}

void inv25519(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; ++a) c[a] = i[a];
    for (int a = 253; a >= 0; --a) {
        S(c, c);
        if (a != 2 && a != 4) M(c, c, i);
    }
    for (int a = 0; a < 16; ++a) o[a] = c[a];
}

void pow2523(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; ++a) c[a] = i[a];
    for (int a = 250; a >= 0; --a) {
        S(c, c);
        if (a != 1) M(c, c, i);
    }
    for (int a = 0; a < 16; ++a) o[a] = c[a];
}

void add(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;
    Z(a, p[1], p[0]);
    Z(t, q[1], q[0]);
    M(a, a, t);
    A(b, p[0], p[1]);
    A(t, q[0], q[1]);
    M(b, b, t);
    M(c, p[3], q[3]);
    M(c, c, kD2);
    M(d, p[2], q[2]);
    A(d, d, d);
    Z(e, b, a);
    Z(f, d, c);
    A(g, d, c);
    A(h, b, a);
    M(p[0], e, f);
    M(p[1], h, g);
    M(p[2], g, f);
    M(p[3], e, h);
}

void cswap(gf p[4], gf q[4], u8 b)
{
    for (int i = 0; i < 4; ++i) sel25519(p[i], q[i], b);
}

void pack(u8 *r, gf p[4])
{
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= static_cast<u8>(par25519(tx) << 7);
}

void scalarmult(gf p[4], gf q[4], const u8 *s)
{
    set25519(p[0], kGf0);
    set25519(p[1], kGf1);
    set25519(p[2], kGf1);
    set25519(p[3], kGf0);
    for (int i = 255; i >= 0; --i) {
        const u8 b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b);
        add(q, p);
        add(p, p);
        cswap(p, q, b);
    }
}

void scalarbase(gf p[4], const u8 *s)
{
    gf q[4];
    set25519(q[0], kX);
    set25519(q[1], kY);
    set25519(q[2], kGf1);
    M(q[3], kX, kY);
    scalarmult(p, q, s);
}

void modL(u8 *r, i64 x[64])
{
    i64 carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * static_cast<i64>(kL[j - (i - 32)]);
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * static_cast<i64>(kL[j]);
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j) x[j] -= carry * static_cast<i64>(kL[j]);
    for (i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = static_cast<u8>(x[i] & 255);
    }
}

void reduce(u8 *r)
{
    i64 x[64];
    for (int i = 0; i < 64; ++i) x[i] = static_cast<i64>(r[i]);
    for (int i = 0; i < 64; ++i) r[i] = 0;
    modL(r, x);
}

bool unpackneg(gf r[4], const u8 p[32])
{
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], kGf1);
    unpack25519(r[1], p);
    S(num, r[1]);
    M(den, num, kD);
    Z(num, num, r[2]);
    A(den, r[2], den);
    S(den2, den);
    S(den4, den2);
    M(den6, den4, den2);
    M(t, den6, num);
    M(t, t, den);
    pow2523(t, t);
    M(t, t, num);
    M(t, t, den);
    M(t, t, den);
    M(r[0], t, den);
    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num)) M(r[0], r[0], kI);
    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num)) return false;
    if (par25519(r[0]) == (p[31] >> 7)) Z(r[0], kGf0, r[0]);
    M(r[3], r[0], r[1]);
    return true;
}

QByteArray sha512(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha512);
}

// S (a segunda metade da assinatura) tem de ser < L: impede assinaturas maleáveis.
bool scalarBelowL(const u8 *s)
{
    for (int i = 31; i >= 0; --i) {
        if (s[i] < kL[i]) return true;
        if (s[i] > kL[i]) return false;
    }
    return false;
}

} // namespace

QByteArray publicKeyFromSecret(const QByteArray &secretSeed)
{
    if (secretSeed.size() != 32) return {};
    QByteArray d = sha512(secretSeed);
    u8 *dd = reinterpret_cast<u8 *>(d.data());
    dd[0] &= 248;
    dd[31] &= 127;
    dd[31] |= 64;
    gf p[4];
    scalarbase(p, dd);
    QByteArray pk(32, 0);
    pack(reinterpret_cast<u8 *>(pk.data()), p);
    return pk;
}

KeyPair generateKeyPair()
{
    KeyPair kp;
    kp.secretKey.resize(32);
    QRandomGenerator *rng = QRandomGenerator::system();
    for (int i = 0; i < 32; ++i) kp.secretKey[i] = static_cast<char>(rng->bounded(256));
    kp.publicKey = publicKeyFromSecret(kp.secretKey);
    return kp;
}

QByteArray sign(const QByteArray &message, const QByteArray &secretSeed)
{
    if (secretSeed.size() != 32) return {};
    const QByteArray pk = publicKeyFromSecret(secretSeed);
    QByteArray d = sha512(secretSeed);
    u8 *dd = reinterpret_cast<u8 *>(d.data());
    dd[0] &= 248;
    dd[31] &= 127;
    dd[31] |= 64;

    QByteArray rInput = d.mid(32, 32) + message;
    QByteArray r = sha512(rInput);
    reduce(reinterpret_cast<u8 *>(r.data()));
    gf p[4];
    scalarbase(p, reinterpret_cast<const u8 *>(r.constData()));
    QByteArray sig(64, 0);
    pack(reinterpret_cast<u8 *>(sig.data()), p);

    QByteArray h = sha512(sig.left(32) + pk + message);
    reduce(reinterpret_cast<u8 *>(h.data()));
    i64 x[64];
    for (int i = 0; i < 64; ++i) x[i] = 0;
    for (int i = 0; i < 32; ++i) x[i] = static_cast<u8>(r[i]);
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j) x[i + j] += static_cast<i64>(static_cast<u8>(h[i])) * static_cast<i64>(dd[j]);
    modL(reinterpret_cast<u8 *>(sig.data()) + 32, x);
    return sig;
}

bool verify(const QByteArray &message, const QByteArray &signature, const QByteArray &publicKey)
{
    if (signature.size() != 64 || publicKey.size() != 32) return false;
    const u8 *sm = reinterpret_cast<const u8 *>(signature.constData());
    if (!scalarBelowL(sm + 32)) return false;
    gf p[4], q[4];
    if (!unpackneg(q, reinterpret_cast<const u8 *>(publicKey.constData()))) return false;
    QByteArray h = sha512(signature.left(32) + publicKey + message);
    reduce(reinterpret_cast<u8 *>(h.data()));
    scalarmult(p, q, reinterpret_cast<const u8 *>(h.constData()));
    scalarbase(q, sm + 32);
    add(p, q);
    u8 t[32];
    pack(t, p);
    return std::memcmp(sm, t, 32) == 0;
}

namespace {

void writeCanonicalString(QByteArray &out, const QString &text)
{
    out += '"';
    for (const char32_t c : text.toUcs4()) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += QByteArray("\\u00") + QByteArray::number(static_cast<int>(c), 16).rightJustified(2, '0');
            } else {
                out += QString::fromUcs4(&c, 1).toUtf8();
            }
        }
    }
    out += '"';
}

void writeCanonical(QByteArray &out, const QJsonValue &value);

void writeCanonicalObject(QByteArray &out, const QJsonObject &obj)
{
    QStringList keys = obj.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) { return a.toUcs4() < b.toUcs4(); });
    out += '{';
    for (int i = 0; i < keys.size(); ++i) {
        if (i > 0) out += ',';
        writeCanonicalString(out, keys[i]);
        out += ':';
        writeCanonical(out, obj.value(keys[i]));
    }
    out += '}';
}

void writeCanonical(QByteArray &out, const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: out += "null"; break;
    case QJsonValue::Bool: out += value.toBool() ? "true" : "false"; break;
    case QJsonValue::Double: {
        const double d = value.toDouble();
        out += (d == static_cast<double>(static_cast<qint64>(d))) ? QByteArray::number(static_cast<qint64>(d))
                                                                  : QByteArray::number(d, 'g', 17);
        break;
    }
    case QJsonValue::String: writeCanonicalString(out, value.toString()); break;
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        out += '[';
        for (int i = 0; i < array.size(); ++i) {
            if (i > 0) out += ',';
            writeCanonical(out, array.at(i));
        }
        out += ']';
        break;
    }
    case QJsonValue::Object: writeCanonicalObject(out, value.toObject()); break;
    }
}

} // namespace

QByteArray canonicalBody(const QJsonObject &indexObject)
{
    QJsonObject copy = indexObject;
    copy.remove(QStringLiteral("signature"));
    QByteArray out;
    writeCanonicalObject(out, copy);
    return out;
}

QString toBase64(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toBase64());
}

QByteArray fromBase64(const QString &text)
{
    const QByteArray raw = QByteArray::fromBase64Encoding(text.trimmed().toLatin1()).decoded;
    return raw;
}

Result verifyIndex(const QJsonObject &indexObject, const QString &publicKeyBase64)
{
    if (publicKeyBase64.isEmpty()) return Result::NoKeyDeclared;
    const QString signature = indexObject.value(QStringLiteral("signature")).toString();
    if (signature.isEmpty()) return Result::Missing;
    return verify(canonicalBody(indexObject), fromBase64(signature), fromBase64(publicKeyBase64)) ? Result::Ok
                                                                                                  : Result::Invalid;
}

} // namespace kai::engine::store_signature
