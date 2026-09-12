#include <QTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImageReader>
#include <QTemporaryDir>

#include "ui/icon-picker-widget.h"

using namespace kai::ui;

// Suporte a arquivos de imagem como ícone customizado, com foco em GIF.
//
// Dois riscos reais cobertos aqui:
//  1) o plugin de imagem do formato não estar disponível no build/deploy — no
//     Windows os plugins são copiados pelo windeployqt (qgif.dll), e sem ele a
//     imagem simplesmente não carrega, sem erro visível;
//  2) formatos ANIMADOS: QIcon não anima (quem desenha é o QTreeWidget/QTabBar,
//     que não têm noção de quadros), então precisamos garantir que ao menos o
//     PRIMEIRO QUADRO seja extraído e resulte num ícone válido — e não num
//     ícone nulo, que na árvore apareceria como espaço vazio.
class TestIconFormats : public QObject {
    Q_OBJECT

private:
    // GIF 89a mínimo, 4x4, com 3 quadros (animado).
    static bool writeAnimatedGif(const QString &path)
    {
        QByteArray d;
        d += "GIF89a";
        const auto le16 = [&d](quint16 v) {
            d += char(v & 0xFF);
            d += char((v >> 8) & 0xFF);
        };
        le16(4); le16(4);
        d += char(0xF6); d += char(0x00); d += char(0x00); // 128 cores globais
        for (int i = 0; i < 128; ++i) {
            d += char(i == 0 ? 255 : 0);
            d += char(i == 1 ? 255 : 0);
            d += char(i == 2 ? 255 : 0);
        }
        d += QByteArray("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19); // loop
        for (int frame = 0; frame < 3; ++frame) {
            d += QByteArray("\x21\xF9\x04\x00\x0A\x00\x00\x00", 8); // GCE
            d += char(0x2C);
            le16(0); le16(0); le16(4); le16(4);
            d += char(0x00);
            // LZW: clear(128) + 16 literais + EOI(129), 8 bits por código
            QByteArray codes;
            codes += char(128);
            for (int p = 0; p < 16; ++p) codes += char(frame);
            codes += char(129);
            QByteArray packed;
            QString bitstream;
            for (char c : codes) {
                QString bits = QString::number(static_cast<quint8>(c), 2).rightJustified(8, '0');
                std::reverse(bits.begin(), bits.end());
                bitstream += bits;
            }
            for (int i = 0; i < bitstream.size(); i += 8) {
                QString byte = bitstream.mid(i, 8).leftJustified(8, '0');
                std::reverse(byte.begin(), byte.end());
                packed += char(byte.toUInt(nullptr, 2));
            }
            d += char(7);                       // LZW min code size
            d += char(packed.size());
            d += packed;
            d += char(0x00);
        }
        d += char(0x3B);

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        return f.write(d) == d.size();
    }

private slots:
    void gifPluginIsAvailable()
    {
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        QVERIFY2(formats.contains(QByteArray("gif")),
                 "plugin de imagem GIF ausente no build (no Windows vem via qgif.dll)");
    }

    void animatedGifYieldsValidIcon()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("anim.gif"));
        QVERIFY2(writeAnimatedGif(path), "falhou ao escrever o GIF de teste");

        // Confirma que é realmente animado (mais de um quadro).
        QImageReader reader(path);
        QVERIFY(reader.canRead());
        QCOMPARE(QString::fromLatin1(reader.format()), QStringLiteral("gif"));
        QVERIFY2(reader.supportsAnimation(), "GIF de teste deveria ser animado");
        QVERIFY2(reader.imageCount() > 1,
                 qPrintable(QStringLiteral("quadros=%1").arg(reader.imageCount())));

        // O caminho que o Kai usa: prefixo "file:" + caminho.
        const QIcon icon = IconPickerWidget::iconForName(QStringLiteral("file:") + path);
        QVERIFY2(!icon.isNull(), "ícone nulo: na árvore apareceria como espaço vazio");
        QVERIFY2(!icon.pixmap(QSize(28, 28)).isNull(), "pixmap do primeiro quadro vazio");
    }

    void missingFileDoesNotCrash()
    {
        // Arquivo removido/movido: ícone nulo em vez de crash.
        const QIcon icon = IconPickerWidget::iconForName(
            QStringLiteral("file:/caminho/que/nao/existe.gif"));
        QVERIFY(icon.isNull());
    }
};

QTEST_MAIN(TestIconFormats)
#include "test_icon_formats.moc"
