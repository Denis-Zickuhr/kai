# Builds via Docker — Linux & Windows

Ambiente containerizado para gerar as distribuições do **Kai** sem sujar a
máquina host com toolchains. Um container builda o binário Linux (+ AppImage)
e outro cross-compila o `.exe` do Windows a partir do Linux.

## Pré-requisitos

- Docker + Docker Compose v2 (`docker compose ...`).
- Os artefatos são escritos em `./dist/` na raiz do repositório.

## Uso rápido (Docker Compose)

```bash
# Constrói as duas imagens de build
docker compose build

# Gera os artefatos Linux (binário + AppImage quando possível) em ./dist
docker compose run --rm build-linux

# Gera os artefatos Windows (kai.exe + DLLs + plugins + assets) em ./dist
docker compose run --rm build-windows
```

## Artefatos gerados

| Plataforma | Caminho | Conteúdo |
|-----------|---------|----------|
| Linux | `dist/Kai-x86_64.AppImage` | AppImage portável (auto-contido) |
| Linux (fallback) | `dist/kai-linux/` | `kai` + `assets/` (se o AppImage falhar) |
| Windows | `dist/kai-windows/` | `kai.exe` + DLLs do Qt + plugins + `assets/` |
| Windows (opcional) | `dist/kai-setup.exe` | Instalador Inno Setup (se wine+ISCC na imagem) |

## Detalhes por plataforma

### Linux — `packaging/docker/Dockerfile.linux`

- Base **Ubuntu 24.04** com Qt 6 do repositório (`qt6-base-dev`, `qt6-svg-dev`).
- O módulo **Svg é obrigatório**: desde a migração dos ícones para Lucide,
  os SVGs são renderizados via `QSvgRenderer` (embarcados no binário através
  do `assets/icons/icons.qrc`).
- Empacota um **AppImage** via `linuxdeploy` + plugin Qt. Se o download das
  ferramentas falhar (build offline), cai para copiar apenas o binário +
  `assets/`, sem abortar.

### Windows — `packaging/docker/Dockerfile.windows`

- Cross-compila com a imagem oficial **`stateoftheartio/qt6:6.6-mingw-aqt`**
  (MinGW + Qt6 via aqtinstall, com wine para o `windeployqt`) — projeto
  [state-of-the-art/qt6-docker](https://github.com/state-of-the-art/qt6-docker).
  Não precisa de Windows nem MSVC.
- O entrypoint instala o módulo **Qt Svg** (os ícones Lucide usam
  `QSvgRenderer`), compila com `qt-cmake` + Ninja e empacota as DLLs +
  plugins com `windeployqt`.
- Opcionalmente gere o instalador com **Inno Setup** a partir do script
  existente `packaging/windows/kai.iss` (num Windows ou via wine).

> A imagem base foi verificada como existente no Docker Hub. Se quiser
> outra versão do Qt, troque a tag em `Dockerfile.windows` (ex:
> `6.7-mingw-aqt`, `6.8-mingw-aqt`).

#### Como capturar o erro exato (faça isto primeiro)

Se "buildou a imagem mas o `.exe` não saiu", rode o container capturando o
log completo — o entrypoint aborta com mensagem clara em cada etapa:

```bash
chmod 777 dist   # garante que o usuário 'user' do container escreva em ./dist
docker compose run --rm build-windows 2>&1 | tee /tmp/kai-win-build.log
echo "exit=$?"
```

Depois verifique o que saiu:

```bash
ls -la dist/kai-windows/        # deve conter kai.exe + *.dll + assets/
ls -la dist/kai-setup.exe       # instalador (opcional)
grep -iE "erro|error|fatal|failed|c0000018" /tmp/kai-win-build.log | tail -30
```

As três falhas mais comuns, e o que cada uma significa:
- **Parou em "Cross-compilando" com `c0000018` / `CreateProcess: Internal
  error`** → é o wine sob WSL2 (veja abaixo). O `.exe` NÃO foi gerado.
- **Gerou `dist/kai-windows/kai.exe` mas sem `.dll`** → `windeployqt` (wine)
  falhou; a pasta portável fica incompleta. Rode de novo com
  `seccomp=unconfined`.
- **Tem a pasta `kai-windows/` completa mas faltou só `kai-setup.exe`** →
  o Inno Setup não estava na imagem e o download falhou (container sem
  internet). A pasta portável já é distribuível; o instalador é opcional.

#### Solução de problemas do build Windows

- **`could not find git for clone of qhotkey-populate`** — já tratado: a
  imagem pré-clona o QHotkey (git do Linux) e o entrypoint aponta o
  FetchContent para essa cópia via `-DFETCHCONTENT_SOURCE_DIR_QHOTKEY`,
  porque o CMake que roda no cross-build (via wine) não enxerga o git do
  Linux.

- **`wine: failed to map the shared user data: c0000018` /
  `ninja: fatal: CreateProcess: Internal error`** — o CMake configura com
  sucesso, mas a compilação real (que invoca o compilador MinGW através do
  wine) falha. Isso é um **problema de ambiente do wine sob WSL2/Docker**,
  não do projeto. Workarounds conhecidos:
  1. Rodar o container com seccomp desabilitado:
     `docker compose run --rm --security-opt seccomp=unconfined build-windows`
     (ou adicionar `security_opt: [seccomp=unconfined]` ao serviço).
  2. Passar `WINEDEBUG=-all` e garantir `/tmp` executável.
  3. Rodar o Docker fora do WSL2 (Linux nativo), onde o wine funciona sem
     essa restrição de mapeamento de memória.
  4. Alternativa sem wine: compilar no próprio Windows com
     `build-windows.bat` (ver `BUILD-WINDOWS.md`).

## Rodar sem Compose (docker puro)

```bash
# Linux
docker build -f packaging/docker/Dockerfile.linux -t kai-build-linux .
docker run --rm -v "$PWD:/src:ro" -v "$PWD/dist:/out" kai-build-linux

# Windows
docker build -f packaging/docker/Dockerfile.windows -t kai-build-windows .
docker run --rm -v "$PWD:/src:ro" -v "$PWD/dist:/out" kai-build-windows
```

O código-fonte é montado **read-only** (`/src:ro`); o build acontece em
`/tmp` dentro do container e só os artefatos finais são escritos em `/out`
(mapeado para `./dist`).
