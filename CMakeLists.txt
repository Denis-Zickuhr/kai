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
    src/core/kai-file-validator.h
    src/core/kai-file-validator.cpp
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
    src/ui/export-dialog.h
    src/ui/export-dialog.cpp
    src/ui/import-dialog.h
    src/ui/import-dialog.cpp
    src/ui/import-selection-dialog.h
    src/ui/import-selection-dialog.cpp
    src/ui/log-line-view.h
    src/ui/log-line-view.cpp
    src/ui/table-utils.h
    src/ui/table-utils.cpp
    src/ui/row-edit-dialog.h
    src/ui/row-edit-dialog.cpp
    src/ui/code-output-view.h
    src/ui/code-output-view.cpp
    src/ui/output-panel.h
    src/ui/output-panel.cpp
    src/ui/pty-terminal-widget.h
    src/ui/pty-terminal-widget.cpp
    src/ui/inline-code-field.h
    src/ui/inline-code-field.cpp
    src/ui/ansi-text-parser.h
    src/ui/ansi-text-parser.cpp
    src/ui/fuzzy-search.h
    src/ui/fuzzy-search.cpp
    src/ui/env-var-autocomplete.h
    src/ui/env-var-autocomplete.cpp
    src/ui/terminal-drawer.h
    src/ui/terminal-drawer.cpp
    src/ui/project-selector.h
    src/ui/project-selector.cpp
    src/ui/project-import-options-dialog.h
    src/ui/project-import-options-dialog.cpp
    src/ui/command-tree-widget.h
    src/ui/command-tree-widget.cpp
    src/ui/draggable-tree-widget.h
    src/ui/collapsible-section-card.h
    src/ui/collapsible-section-card.cpp
    src/ui/parameter-form-dialog.h
    src/ui/parameter-form-dialog.cpp
    src/ui/key-value-editor-widget.h
    src/ui/key-value-editor-widget.cpp
    src/ui/terminal-profiles-editor-widget.h
    src/ui/terminal-profiles-editor-widget.cpp
    src/ui/parameter-editor-widget.h
    src/ui/parameter-editor-widget.cpp
    src/ui/output-responders-editor-widget.h
    src/ui/output-responders-editor-widget.cpp
    src/ui/hooks-editor-widget.h
    src/ui/hooks-editor-widget.cpp
    src/ui/execution-conditions-editor-widget.h
    src/ui/execution-conditions-editor-widget.cpp
    src/ui/env-extractors-editor-widget.h
    src/ui/env-extractors-editor-widget.cpp
    src/ui/declared-env-vars-editor-widget.h
    src/ui/declared-env-vars-editor-widget.cpp
    src/ui/dynamic-vars-inspector-widget.h
    src/ui/dynamic-vars-inspector-widget.cpp
    src/ui/project-detection-strategy.h
    src/ui/project-detection-strategy.cpp
    src/ui/folder-editor-dialog.h
    src/ui/folder-editor-dialog.cpp
    src/ui/folder-delete-dialog.cpp
    src/ui/command-editor-dialog.h
    src/ui/command-editor-dialog.cpp
    src/ui/command-json-editor-dialog.h
    src/ui/command-json-editor-dialog.cpp
    src/ui/top-utility-bar.h
    src/ui/top-utility-bar.cpp
    src/ui/settings-dialog.h
    src/ui/settings-dialog.cpp
    src/ui/help-dialog.h
    src/ui/help-dialog.cpp
    src/ui/help-content.cpp
    src/ui/quick-body-editor-dialog.h
    src/ui/quick-body-editor-dialog.cpp
    src/ui/json-editor-dialog.h
    src/ui/json-editor-dialog.cpp
    src/ui/json-syntax-highlighter.h
    src/ui/json-syntax-highlighter.cpp
    src/ui/lucide-icons.h
    src/ui/lucide-icons.cpp
    src/ui/icon-picker-widget.h
    src/ui/icon-picker-widget.cpp
    src/ui/icon-picker-dialog.h
    src/ui/icon-picker-dialog.cpp
    src/ui/collection-editor-dialog.h
    src/ui/collection-editor-dialog.cpp
    src/ui/json-viewer-widget.h
    src/ui/json-viewer-widget.cpp
    src/ui/foldable-json-view.h
    src/ui/foldable-json-view.cpp
    src/ui/loading-overlay.h
    src/ui/loading-overlay.cpp
    src/ui/no-scroll-combo-filter.h
    src/ui/collection-selector-dialog.h
    src/ui/collection-selector-dialog.cpp
    src/ui/environment-manager-dialog.h
    src/ui/environment-manager-dialog.cpp
    src/ui/run-history-dialog.h
    src/ui/run-history-dialog.cpp
    src/ui/notification-history-dialog.h
    src/ui/notification-history-dialog.cpp
    assets/icons/icons.qrc
    src/ui/action-sidebar.h
    src/ui/action-sidebar.cpp
    src/ui/shortcut-capture-field.h
    src/ui/shortcut-capture-field.cpp
    src/ui/process-list-dialog.h
    src/ui/process-list-dialog.cpp
    src/ui/log-viewer-dialog.h
    src/ui/log-viewer-dialog.cpp
    src/ui/main-window.h
    src/ui/main-window.cpp
    src/ui/welcome-screen.h
    src/ui/welcome-screen.cpp
    src/ui/expand-collapse-bar.h
    src/ui/expand-collapse-bar.cpp
    src/ui/item-actions-bar.h
    src/ui/item-actions-bar.cpp
    src/ui/action-group-container.h
    src/ui/action-group-container.cpp
    src/ui/shortcuts-manager-widget.h
    src/ui/shortcuts-manager-widget.cpp
    src/ui/storage-manager-widget.h
    src/ui/storage-manager-widget.cpp
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

# --- Binário principal ---
# WIN32: no Windows, marca o executável como GUI app (subsystem WINDOWS),
# evitando que um console (terminal) seja aberto ao rodar o Kai — que é um
# app de bandeja/GUI. Em Linux/macOS a flag é ignorada pelo CMake, sem
# efeito no build nativo.
add_executable(kai WIN32 src/main.cpp)
target_link_libraries(kai PRIVATE kai-core kai-engine kai-ui kai-ipc Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

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
