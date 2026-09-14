cmake_minimum_required(VERSION 3.16)
project(kai VERSION 1.0.1 LANGUAGES CXX C)

# Versão exibida na Ajuda (aba "Versão"). Deriva da versão do project().
set(KAI_VERSION_STRING "${PROJECT_VERSION}")
add_compile_definitions(KAI_VERSION_STRING="${KAI_VERSION_STRING}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg Test)

# --- QHotkey: atalho global cross-platform ---
include(FetchContent)
FetchContent_Declare(
    qhotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    GIT_TAG        1.5.0
)
set(QHOTKEY_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(qhotkey)

# --- libvterm: emulador de terminal (VT100/xterm) para a Saída interativa ---
# Mesma engine usada pelo terminal embutido do Neovim/Vim 8. C puro, sem
# dependências. NÃO tem CMakeLists.txt upstream (build via Makefile простo),
# então populamos o source e montamos a lib nós mesmos, em vez do
# FetchContent_MakeAvailable usado para o QHotkey (que exige um
# CMakeLists.txt no repositório baixado — libvterm não tem).
FetchContent_Declare(
    libvterm
    GIT_REPOSITORY https://github.com/neovim/libvterm.git
    GIT_TAG        934bc2fbf21800ac3458a499df8820ca5fb45fd3 # branch nvim, pinado
)
FetchContent_GetProperties(libvterm)
if(NOT libvterm_POPULATED)
    FetchContent_Populate(libvterm)
endif()

add_library(vterm STATIC
    ${libvterm_SOURCE_DIR}/src/encoding.c
    ${libvterm_SOURCE_DIR}/src/keyboard.c
    ${libvterm_SOURCE_DIR}/src/mouse.c
    ${libvterm_SOURCE_DIR}/src/parser.c
    ${libvterm_SOURCE_DIR}/src/pen.c
    ${libvterm_SOURCE_DIR}/src/screen.c
    ${libvterm_SOURCE_DIR}/src/state.c
    ${libvterm_SOURCE_DIR}/src/unicode.c
    ${libvterm_SOURCE_DIR}/src/vterm.c
)
target_include_directories(vterm PUBLIC ${libvterm_SOURCE_DIR}/include)
target_compile_features(vterm PRIVATE c_std_99)
# Código de terceiros: silencia os warnings PRÓPRIOS dele (não são nossos
# pra corrigir) em vez de herdar o -Wall -Wextra global do projeto.
target_compile_options(vterm PRIVATE -w)

# --- Biblioteca "kai-core": modelos, config e ambiente ---
# Compartilhada entre o binário principal e os testes, para evitar
# recompilar as mesmas fontes duas vezes.
add_library(kai-core STATIC
    src/core/models.h
    src/core/models.cpp
    src/core/config-manager.h
    src/core/config-manager.cpp
    src/core/environment-manager.h
    src/core/environment-manager.cpp
    src/core/run-history.h
    src/core/run-history.cpp
    src/core/notification-history.h
    src/core/notification-history.cpp
    src/core/curl-parser.h
    src/core/curl-parser.cpp
    src/core/openapi-parser.h
    src/core/openapi-parser.cpp
    src/core/yaml-bridge.h
    src/core/yaml-bridge.cpp
    src/core/date-param-format.h
    src/core/date-param-format.cpp
    src/core/kai-file-validator.h
    src/core/kai-file-validator.cpp
    src/core/cli-path-resolver.h
    src/core/cli-path-resolver.cpp
    src/core/cli-param-binder.h
    src/core/cli-param-binder.cpp
    src/core/cli-reserved-verbs.h
    src/core/folder-path-resolver.h
    src/utils/path-format.h
    src/utils/path-format.cpp
    src/utils/asset-paths.h
    src/utils/logger.h
    src/utils/asset-paths.cpp
    src/utils/logger.cpp
    src/utils/autostart-manager.h
    src/utils/autostart-manager.cpp
    src/utils/theme-manager.h
    src/utils/theme-manager.cpp
    src/utils/translation-manager.h
    src/utils/translation-manager.cpp
)

target_include_directories(kai-core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kai-core PUBLIC Qt6::Core Qt6::Network)

# --- Biblioteca "kai-engine": execução de processos, pipeline e HTTP ---
add_library(kai-engine STATIC
    src/engine/process-runner.h
    src/engine/process-runner.cpp
    src/engine/process-manager.h
    src/engine/process-manager.cpp
    src/engine/http-runner.h
    src/engine/http-runner.cpp
    src/engine/execution-pipeline.h
    src/engine/execution-pipeline.cpp
    src/engine/output-responder-matcher.h
    src/engine/output-responder-matcher.cpp
)

target_include_directories(kai-engine PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kai-engine PUBLIC kai-core Qt6::Core Qt6::Network)

# --- Biblioteca "kai-ui": widgets Qt ---
add_library(kai-ui STATIC
    src/utils/design-tokens.h
    src/utils/design-tokens.cpp
    src/utils/action-shortcuts.h
    src/utils/action-shortcuts.cpp
    src/ui/app-stylesheet.h
    src/ui/app-stylesheet.cpp
    src/ui/features/collections/export-dialog.h
    src/ui/features/collections/export-dialog.cpp
    src/ui/features/collections/import-dialog.h
    src/ui/features/collections/import-dialog.cpp
    src/ui/features/collections/import-selection-dialog.h
    src/ui/features/collections/import-selection-dialog.cpp
    src/ui/features/output/log-line-view.h
    src/ui/features/output/log-line-view.cpp
    src/ui/shared/table-utils.h
    src/ui/shared/table-utils.cpp
    src/ui/shared/row-edit-dialog.h
    src/ui/shared/row-edit-dialog.cpp
    src/ui/shared/date-picker-dialog.h
    src/ui/shared/date-picker-dialog.cpp
    src/ui/features/output/code-output-view.h
    src/ui/features/output/code-output-view.cpp
    src/ui/features/output/output-panel.h
    src/ui/features/output/output-panel.cpp
    src/ui/features/output/pty-terminal-widget.h
    src/ui/features/output/pty-terminal-widget.cpp
    src/ui/shared/inline-code-field.h
    src/ui/shared/inline-code-field.cpp
    src/ui/features/output/ansi-text-parser.h
    src/ui/features/output/ansi-text-parser.cpp
    src/ui/shared/fuzzy-search.h
    src/ui/shared/fuzzy-search.cpp
    src/ui/features/environments/env-var-autocomplete.h
    src/ui/features/environments/env-var-autocomplete.cpp
    src/ui/features/output/terminal-drawer.h
    src/ui/features/output/terminal-drawer.cpp
    src/ui/features/collections/project-selector.h
    src/ui/features/collections/project-selector.cpp
    src/ui/features/collections/project-import-options-dialog.h
    src/ui/features/collections/project-import-options-dialog.cpp
    src/ui/features/command-editor/command-tree-widget.h
    src/ui/features/command-editor/command-tree-widget.cpp
    src/ui/shared/draggable-tree-widget.h
    src/ui/shared/draggable-table-widget.h
    src/ui/shared/collapsible-section-card.h
    src/ui/shared/collapsible-section-card.cpp
    src/ui/features/command-editor/parameter-form-dialog.h
    src/ui/features/command-editor/parameter-form-dialog.cpp
    src/ui/shared/key-value-editor-widget.h
    src/ui/shared/key-value-editor-widget.cpp
    src/ui/features/output/terminal-profiles-editor-widget.h
    src/ui/features/output/terminal-profiles-editor-widget.cpp
    src/ui/features/command-editor/parameter-editor-widget.h
    src/ui/features/command-editor/parameter-editor-widget.cpp
    src/ui/features/command-editor/output-responders-editor-widget.h
    src/ui/features/command-editor/output-responders-editor-widget.cpp
    src/ui/features/command-editor/hooks-editor-widget.h
    src/ui/features/command-editor/hooks-editor-widget.cpp
    src/ui/features/command-editor/execution-conditions-editor-widget.h
    src/ui/features/command-editor/execution-conditions-editor-widget.cpp
    src/ui/features/environments/env-extractors-editor-widget.h
    src/ui/features/environments/env-extractors-editor-widget.cpp
    src/ui/features/environments/declared-env-vars-editor-widget.h
    src/ui/features/environments/declared-env-vars-editor-widget.cpp
    src/ui/features/environments/dynamic-vars-inspector-widget.h
    src/ui/features/environments/dynamic-vars-inspector-widget.cpp
    src/ui/features/collections/project-detection-strategy.h
    src/ui/features/collections/project-detection-strategy.cpp
    src/ui/features/collections/folder-editor-dialog.h
    src/ui/features/collections/folder-editor-dialog.cpp
    src/ui/features/collections/folder-delete-dialog.cpp
    src/ui/features/command-editor/command-editor-dialog.h
    src/ui/features/command-editor/command-editor-dialog.cpp
    src/ui/features/command-editor/command-json-editor-dialog.h
    src/ui/features/command-editor/command-json-editor-dialog.cpp
    src/ui/shared/top-utility-bar.h
    src/ui/shared/top-utility-bar.cpp
    src/ui/features/settings/settings-dialog.h
    src/ui/features/settings/settings-dialog.cpp
    src/ui/features/settings/tabs/general-tab.h
    src/ui/features/settings/tabs/general-tab.cpp
    src/ui/features/settings/tabs/appearance-tab.h
    src/ui/features/settings/tabs/appearance-tab.cpp
    src/ui/features/settings/tabs/shortcuts-tab.h
    src/ui/features/settings/tabs/shortcuts-tab.cpp
    src/ui/features/settings/tabs/terminals-tab.h
    src/ui/features/settings/tabs/terminals-tab.cpp
    src/ui/features/settings/tabs/notifications-tab.h
    src/ui/features/settings/tabs/notifications-tab.cpp
    src/ui/shared/help-dialog.h
    src/ui/shared/help-dialog.cpp
    src/ui/shared/help-content.cpp
    src/ui/shared/quick-body-editor-dialog.h
    src/ui/shared/quick-body-editor-dialog.cpp
    src/ui/shared/json-editor-dialog.h
    src/ui/shared/json-editor-dialog.cpp
    src/ui/shared/json-syntax-highlighter.h
    src/ui/shared/json-syntax-highlighter.cpp
    src/ui/shared/lucide-icons.h
    src/ui/shared/lucide-icons.cpp
    src/ui/shared/icon-picker-widget.h
    src/ui/shared/icon-picker-widget.cpp
    src/ui/shared/icon-picker-dialog.h
    src/ui/shared/icon-picker-dialog.cpp
    src/ui/features/collections/collection-editor-dialog.h
    src/ui/features/collections/collection-editor-dialog.cpp
    src/ui/shared/json-viewer-widget.h
    src/ui/shared/json-viewer-widget.cpp
    src/ui/shared/foldable-json-view.h
    src/ui/shared/foldable-json-view.cpp
    src/ui/shared/loading-overlay.h
    src/ui/shared/loading-overlay.cpp
    src/ui/shared/no-scroll-combo-filter.h
    src/ui/features/collections/collection-selector-dialog.h
    src/ui/features/collections/collection-selector-dialog.cpp
    src/ui/features/environments/environment-manager-dialog.h
    src/ui/features/environments/environment-manager-dialog.cpp
    src/ui/features/history/run-history-dialog.h
    src/ui/features/history/run-history-dialog.cpp
    src/ui/features/history/notification-history-dialog.h
    src/ui/features/history/notification-history-dialog.cpp
    assets/icons/icons.qrc
    src/ui/shared/action-sidebar.h
    src/ui/shared/action-sidebar.cpp
    src/ui/shared/shortcut-capture-field.h
    src/ui/shared/shortcut-capture-field.cpp
    src/ui/features/history/process-list-dialog.h
    src/ui/features/history/process-list-dialog.cpp
    src/ui/features/output/log-viewer-dialog.h
    src/ui/features/output/log-viewer-dialog.cpp
    src/ui/main-window.h
    src/ui/main-window.cpp
    src/ui/shared/welcome-screen.h
    src/ui/shared/welcome-screen.cpp
    src/ui/shared/expand-collapse-bar.h
    src/ui/shared/expand-collapse-bar.cpp
    src/ui/shared/item-actions-bar.h
    src/ui/shared/item-actions-bar.cpp
    src/ui/shared/action-group-container.h
    src/ui/shared/action-group-container.cpp
    src/ui/shared/shortcuts-manager-widget.h
    src/ui/shared/shortcuts-manager-widget.cpp
    src/ui/shared/storage-manager-widget.h
    src/ui/shared/storage-manager-widget.cpp
)

target_include_directories(kai-ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kai-ui PUBLIC kai-core kai-engine Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg QHotkey::QHotkey vterm)

# Windows: dwmapi fornece DwmSetWindowAttribute, usado pelo backdrop nativo
# (Mica/Acrylic) do modo "material líquido". user32 fornece
# SetForegroundWindow/AttachThreadInput/BringWindowToTop, usados pra
# forçar a janela pra frente ao reexibir via atalho GLOBAL (ver
# forceForegroundOnWindows em main-window.cpp) — normalmente já vem
# linkada transitivamente por Qt, mas explícito evita depender disso.
if(WIN32)
    target_link_libraries(kai-ui PRIVATE dwmapi user32)
endif()

# --- Biblioteca "kai-ipc": servidor/cliente CLI via QLocalServer ---
add_library(kai-ipc STATIC
    src/ipc/ipc-server.h
    src/ipc/ipc-server.cpp
    src/ipc/cli-client.h
    src/ipc/cli-client.cpp
)
target_include_directories(kai-ipc PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kai-ipc PUBLIC kai-core Qt6::Core Qt6::Network)

# --- Biblioteca "kai-cli": modo LOCAL de CLI Paths (ver src/cli/cli-local-
# executor.h) — fica ACIMA de kai-ui na hierarquia de dependência (kai-ui
# NÃO pode depender dela, senão vira ciclo), porque precisa reaproveitar
# tanto ui::ProjectSelector (parser de kai.json/kai.yml, vive em kai-ui)
# quanto engine::ExecutionPipeline (motor de execução, vive em kai-engine)
# juntos — nenhuma das duas bibliotecas existentes pode depender da outra
# na direção que isto exigiria.
add_library(kai-cli STATIC
    src/cli/cli-local-executor.h
    src/cli/cli-local-executor.cpp
)
target_include_directories(kai-cli PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kai-cli PUBLIC kai-core kai-engine kai-ui Qt6::Core Qt6::Widgets Qt6::Network)

# --- Binário principal ---
# WIN32: no Windows, marca o executável como GUI app (subsystem WINDOWS),
# evitando que um console (terminal) seja aberto ao rodar o Kai — que é um
# app de bandeja/GUI. Em Linux/macOS a flag é ignorada pelo CMake, sem
# efeito no build nativo.
add_executable(kai WIN32 src/main.cpp)
target_link_libraries(kai PRIVATE kai-core kai-engine kai-ui kai-ipc kai-cli Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# Ícone do executável no Windows: compila o recurso .rc (que referencia
# packaging/windows/kai.ico) e o adiciona ao alvo. Sem isto, o Windows
# mostra um ícone genérico na barra de tarefas/Explorer/alt-tab. Em
# Linux/macOS este bloco é ignorado.
if(WIN32)
    enable_language(RC)
    target_sources(kai PRIVATE packaging/windows/kai.rc)
    set_source_files_properties(packaging/windows/kai.rc PROPERTIES
        COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/packaging/windows")
endif()

# --- Testes (Qt Test) ---
# Podem ser desligados no cross-compile Windows (onde compilar os testes
# sob wine é instável e desnecessário para gerar o kai.exe/instalador).
# No Linux ficam ON por padrão. Uso: -DKAI_BUILD_TESTS=OFF
option(KAI_BUILD_TESTS "Compila a suíte de testes (Qt Test)" ON)
if(KAI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
