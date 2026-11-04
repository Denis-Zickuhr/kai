#pragma once

namespace kai::core {

// Situação de uma pasta-projeto já sincronizada em relação ao último sync (manual): o que
// mudou desde então no Kai e/ou no arquivo kai.yml.
enum class SyncDrift {
    InSync,       // nada mudou (ou os dois lados ficaram iguais)
    KaiChanged,   // o Kai mudou e o arquivo ainda não tem
    FileChanged,  // o arquivo mudou (ou sumiu) e o Kai ainda não tem
    BothChanged,  // os dois mudaram
};

} // namespace kai::core
