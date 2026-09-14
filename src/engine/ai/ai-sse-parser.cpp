#include "engine/ai/ai-sse-parser.h"

namespace kai::engine {

void AiSseParser::feed(const QByteArray &chunk)
{
    m_buffer.append(chunk);
    int start = 0;
    while (true) {
        const int nl = m_buffer.indexOf('\n', start);
        if (nl < 0) {
            break;
        }
        QByteArray line = m_buffer.mid(start, nl - start);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        processLine(line);
        start = nl + 1;
    }
    m_buffer.remove(0, start);
}

void AiSseParser::finish()
{
    if (!m_buffer.isEmpty()) {
        QByteArray line = m_buffer;
        m_buffer.clear();
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        processLine(line);
    }
    dispatch();
}

void AiSseParser::processLine(const QByteArray &line)
{
    if (line.isEmpty()) {
        dispatch();
        return;
    }
    if (line.startsWith(':')) {
        return; // comentário/keep-alive
    }
    const int colon = line.indexOf(':');
    const QByteArray field = colon < 0 ? line : line.left(colon);
    QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 1);
    if (value.startsWith(' ')) {
        value.remove(0, 1);
    }
    if (field == "event") {
        m_event = QString::fromUtf8(value);
    } else if (field == "data") {
        if (m_hasData) {
            m_data.append('\n');
        }
        m_data.append(value);
        m_hasData = true;
    }
}

void AiSseParser::dispatch()
{
    if (m_hasData && m_handler) {
        m_handler(m_event, m_data);
    }
    m_event.clear();
    m_data.clear();
    m_hasData = false;
}

} // namespace kai::engine
