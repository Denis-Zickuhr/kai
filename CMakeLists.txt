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
    src/core/project-format.h
    src/core/project-format.cpp
    src/core/folder-actions.h
    src/core/field-scopes.cpp
    src/core/folder-actions.cpp
    src/core/working-dir.h
    src/core/working-dir.cpp
    src/core/terminal-target.h
    src/core/terminal-target.cpp
    src/core/cli-param-binder.h
    src/core/cli-param-binder.cpp
    src/core/cli-reserved-verbs.h
    src/core/kip-protocol.h
    src/core/kip-protocol.cpp
    src/core/kip-settings.h
    src/core/kip-settings.cpp
    src/core/ipc-endpoint.h
    src/core/ipc-endpoint.cpp
    src/core/interpreter-settings.h
    src/core/interpreter-settings.cpp
    src/core/kip-screen-state.h
    src/core/kip-screen-state.cpp
    src/core/kip-cli-builder.h
    src/core/kip-cli-builder.cpp
    src/core/folder-path-resolver.h
    src/utils/console-context.h
    src/utils/duration-format.h
    src/utils/console-context.cpp
    src/utils/last-directory.h
    src/utils/last-directory.cpp
    src/utils/path-format.h
    src/utils/path-format.cpp
    src/utils/asset-paths.h
    src/utils/logger.h
    src/utils/asset-paths.cpp
    src/utils/logger.cpp
    src/utils/crash-reporter.h
    src/utils/crash-reporter.cpp
    src/utils/autostart-manager.h
    src/utils/autostart-manager.cpp
    src/utils/theme-manager.h
    src/utils/theme-manager.cpp
    src/utils/translation-manager.h
    src/utils/translation-manager.cpp
    src/utils/cron-expression.h
    src/utils/cron-expression.cpp
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
    src/engine/cron-scheduler.h
    src/engine/cron-scheduler.cpp
    src/engine/project-sync-manager.h
    src/engine/project-sync-manager.cpp
    src/engine/kip-session.h
    src/engine/kip-session.cpp
    src/engine/command-language.h
    src/engine/command-language.cpp
    src/engine/script-spill.h
    src/engine/script-spill.cpp
    src/engine/module-files.h
    src/engine/module-files.cpp
    src/engine/kip-modules/kip-modules.qrc
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
    src/ui/features/output/output-tabs-bar.h
    src/ui/features/output/output-tabs-bar.cpp
    src/ui/features/output/quick-run-popup.h
    src/ui/features/output/quick-run-popup.cpp
    src/ui/features/docs/doc-links.h
    src/ui/features/docs/doc-links.cpp
    src/ui/features/docs/mermaid-flowchart.h
    src/ui/features/docs/mermaid-flowchart.cpp
    src/ui/features/docs/doc-style.h
    src/ui/features/docs/doc-style.cpp
    src/ui/features/docs/doc-nav-bar.h
    src/ui/features/docs/doc-nav-bar.cpp
    src/ui/features/docs/doc-viewer.h
    src/ui/features/docs/doc-viewer.cpp
    src/ui/features/docs/doc-file-sidebar.h
    src/ui/features/docs/doc-file-sidebar.cpp
    src/ui/features/docs/doc-text-tools.h
    src/ui/features/docs/doc-text-tools.cpp
    src/ui/features/docs/doc-highlighter.h
    src/ui/features/docs/doc-highlighter.cpp
    src/ui/features/docs/doc-code-editor.h
    src/ui/features/docs/doc-code-editor.cpp
    src/ui/features/docs/doc-code-editor-keys.cpp
    src/ui/features/docs/doc-editor-pane.h
    src/ui/features/docs/doc-editor-pane.cpp
    src/ui/features/docs/doc-tools-menu.h
    src/ui/features/docs/doc-tools-menu.cpp
    src/ui/features/docs/doc-edit-bar.h
    src/ui/features/docs/doc-edit-bar.cpp
    src/ui/features/docs/note-dialog.h
    src/ui/features/docs/note-dialog.cpp
    src/ui/features/docs/doc-search-bar.h
    src/ui/features/docs/doc-search-bar.cpp
    src/ui/features/output/aba-content.h
    src/ui/features/output/aba-content.cpp
    src/ui/features/output/output-metrics-header.h
    src/ui/features/output/output-metrics-header.cpp
    src/ui/features/output/output-stdout-content.h
    src/ui/features/output/output-stdout-content.cpp
    src/ui/features/output/output-json-content.h
    src/ui/features/output/output-json-content.cpp
    src/ui/features/output/output-http-request-content.h
    src/ui/features/output/output-http-request-content.cpp
    src/ui/features/output/output-http-headers-content.h
    src/ui/features/output/output-http-headers-content.cpp
    src/ui/features/output/pty-terminal-widget.h
    src/ui/features/output/pty-terminal-widget.cpp
    src/ui/shared/inline-code-field.h
    src/ui/shared/inline-code-field.cpp
    src/ui/shared/parameter-field-factory.h
    src/ui/shared/parameter-field-factory.cpp
    src/ui/features/output/ansi-text-parser.h
    src/ui/features/output/ansi-text-parser.cpp
    src/ui/shared/fuzzy-search.h
    src/ui/shared/fuzzy-search.cpp
    src/ui/shared/flow-layout.h
    src/ui/shared/flow-layout.cpp
    src/ui/shared/collection-chip-picker.h
    src/ui/shared/collection-chip-picker.cpp
    src/ui/shared/folder-picker-widget.h
    src/ui/shared/folder-picker-widget.cpp
    src/ui/shared/actions-editor-widget.h
    src/ui/shared/actions-editor-widget.cpp
    src/ui/shared/working-dir-field.h
    src/ui/shared/working-dir-field.cpp
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
    src/ui/features/command-editor/folder-actions-view.h
    src/ui/features/command-editor/folder-actions-view.cpp
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
    src/ui/features/command-editor/tabs/general-tab.cpp
    src/ui/features/command-editor/tabs/configuration-tab.cpp
    src/ui/features/command-editor/tabs/command-kip-tab.cpp
    src/ui/features/command-editor/tabs/cli-tab.cpp
    src/ui/features/command-editor/tabs/headers-tab.cpp
    src/ui/features/command-editor/tabs/extractors-tab.cpp
    src/ui/features/command-editor/tabs/declared-env-vars-tab.cpp
    src/ui/features/command-editor/tabs/params-tab.cpp
    src/ui/features/command-editor/tabs/responders-tab.cpp
    src/ui/features/command-editor/tabs/conditions-tab.cpp
    src/ui/features/command-editor/tabs/hooks-tab.cpp
    src/ui/features/command-editor/command-json-editor-dialog.h
    src/ui/features/command-editor/command-json-editor-dialog.cpp
    src/ui/shared/top-utility-bar.h
    src/ui/shared/window-control-buttons.h
    src/ui/shared/window-control-buttons.cpp
    src/ui/shared/window-translucency.h
    src/ui/shared/window-translucency.cpp
    src/ui/shared/dialog-frame.h
    src/ui/shared/dialog-frame.cpp
    src/ui/shared/native-window-corners.h
    src/ui/shared/native-window-corners.cpp
    src/ui/shared/app-window-frame.h
    src/ui/shared/app-window-frame.cpp
    src/ui/shared/top-utility-bar.cpp
    src/ui/features/settings/settings-dialog.h
    src/ui/features/settings/settings-dialog.cpp
    src/ui/features/settings/tabs/general-tab.h
    src/ui/features/settings/tabs/general-tab.cpp
    src/ui/features/settings/tabs/appearance-tab.h
    src/ui/features/settings/tabs/appearance-tab.cpp
    src/ui/features/settings/tabs/layout-tab.h
    src/ui/features/settings/tabs/layout-tab.cpp
    src/ui/features/settings/tabs/shortcuts-tab.h
    src/ui/features/settings/tabs/shortcuts-tab.cpp
    src/ui/features/settings/tabs/terminals-tab.h
    src/ui/features/settings/tabs/terminals-tab.cpp
    src/ui/features/settings/tabs/notifications-tab.h
    src/ui/features/settings/tabs/languages-tab.h
    src/ui/features/settings/tabs/languages-tab.cpp
    src/ui/features/settings/tabs/actions-tab.h
    src/ui/features/settings/tabs/actions-tab.cpp
    src/ui/features/settings/tabs/kip-tab.h
    src/ui/features/settings/tabs/kip-tab.cpp
    src/ui/features/settings/tabs/notifications-tab.cpp
    src/ui/shared/manifesto-dialog.h
    src/ui/shared/manifesto-dialog.cpp
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
    src/ui/shared/file-dialogs.h
    src/ui/shared/file-dialogs.cpp
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
    src/ui/features/kip/kip-chips.h
    src/ui/features/kip/kip-chips.cpp
    src/ui/features/kip/kip-blocks.h
    src/ui/features/kip/kip-blocks.cpp
    src/ui/features/kip/kip-result-card.h
    src/ui/features/kip/kip-result-card.cpp
    src/ui/features/kip/kip-details-drawer.h
    src/ui/features/kip/kip-details-drawer.cpp
    src/ui/features/kip/kip-view.h
    src/ui/features/kip/kip-view.cpp
    src/ui/main-window.h
    src/ui/main-window.cpp
    src/ui/external-run-session.h
    src/ui/external-run-session.cpp
    src/ui/shared/welcome-screen.h
    src/ui/shared/welcome-screen.cpp
    src/ui/shared/expand-collapse-bar.h
    src/ui/shared/expand-collapse-bar.cpp
    src/ui/shared/overflow-indicator.h
    src/ui/shared/overflow-indicator.cpp
    src/ui/shared/panel-frame.h
    src/ui/shared/panel-frame.cpp
    src/ui/shared/tab-strip-background.h
    src/ui/shared/tab-strip-background.cpp
    src/ui/shared/item-actions-bar.h
    src/ui/shared/item-actions-bar.cpp
    src/ui/shared/combo-popup-filter.h
    src/ui/shared/combo-popup-filter.cpp
    src/ui/shared/key-trace-filter.h
    src/ui/shared/key-trace-filter.cpp
    src/ui/shared/status-line.h
    src/ui/shared/status-line.cpp
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
    src/ipc/stream-channel.h
    src/ipc/stream-channel.cpp
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
    src/cli/terminal-mode-filter.h
    src/cli/terminal-mode-filter.cpp
    src/cli/app-delegate.h
    src/cli/cli-completion.h
    src/cli/cli-help-format.h
    src/cli/cli-app-verbs.h
    src/cli/cli-init.h
    src/cli/cli-init.cpp
    src/cli/cli-kip-helper.h
    src/cli/cli-kip-helper.cpp
    src/cli/cli-app-verbs.cpp
    src/cli/cli-help-format.cpp
    src/cli/cli-completion.cpp
    src/cli/completion-installer.h
    src/cli/completion-installer.cpp
    src/cli/terminal-input.h
    src/cli/terminal-input.cpp
    src/cli/app-delegate.cpp
)
target_include_directories(kai-cli PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kai-cli PUBLIC kai-core kai-engine kai-ui kai-ipc Qt6::Core Qt6::Widgets Qt6::Network)

# --- Binário principal ---
# WIN32: no Windows, marca o executável como GUI app (subsystem WINDOWS),
# evitando que um console (terminal) seja aberto ao rodar o Kai — que é um
# app de bandeja/GUI. Em Linux/macOS a flag é ignorada pelo CMake, sem
# efeito no build nativo.
add_executable(kai WIN32 src/main.cpp)
target_link_libraries(kai PRIVATE kai-core kai-engine kai-ui kai-ipc kai-cli Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
if(UNIX AND NOT APPLE)
    # Nomes de função na pilha gravada pelo crash-reporter (backtrace_symbols_fd só vê símbolos exportados).
    target_link_options(kai PRIVATE -rdynamic)
endif()

# kai.com (só Windows): o lado de CONSOLE do kai.exe. O kai.exe é GUI (sem console próprio) e, num terminal que
# hospeda o shell num pseudo-console (o do PhpStorm), a saída do CLI some. Este programa de console é o `kai` que o
# terminal acha primeiro (.com vem antes de .exe no PATHEXT): roda o kai.exe ao lado, com os mesmos argumentos, e
# espera. Ver src/console-shim.cpp. Só WinAPI: linkado estático, sem DLLs para distribuir.
if(WIN32)
    add_executable(kai-console src/console-shim.cpp)
    set_target_properties(kai-console PROPERTIES OUTPUT_NAME kai SUFFIX ".com")
    if(MINGW)
        target_link_options(kai-console PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

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
