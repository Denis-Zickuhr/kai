#pragma once

#include <QString>

namespace kai::utils {

// Último diretório que o usuário usou num seletor de arquivo/pasta (importar, exportar, parâmetros,
// ícones...). Todos os diálogos de arquivo abrem nele — substitui a antiga "pasta inicial" por
// parâmetro. Fica em <config>/last-directory.txt e só vale enquanto o diretório existir.
class LastDirectory {
public:
    // Diretório lembrado que ainda existe; vazio se não há (o diálogo usa o padrão do sistema).
    static QString get();
    // Guarda o diretório de `path`: um arquivo vale pela pasta onde está, uma pasta por ela mesma.
    // Vazio ou inexistente é ignorado.
    static void remember(const QString &path);
    // Onde abrir um diálogo: `hint` (o caminho que o campo já tem) se existir; senão o último diretório.
    static QString startFor(const QString &hint = QString());
    // Para "salvar como": o último diretório + o nome sugerido (só o nome, se não há diretório).
    static QString saveStartFor(const QString &suggestedName);
    // Só para testes: esquece o que está em memória e aponta para outro arquivo.
    static void resetForTests(const QString &filePath = QString());
};

} // namespace kai::utils
