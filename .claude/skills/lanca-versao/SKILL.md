---
name: lanca-versao
description: Fluxo completo de release do Kai (auditoria, commits, PR com CI verde, tag, release). Use quando o usuário disser "lança versão <Tag>" (ex.: "lança versão beta.4").
---

# Lançar versão `<Tag>`

Executar de ponta a ponta, com autonomia. Só parar por bloqueio externo real
(credencial, autorização, decisão do usuário), explicando objetivamente.

## Convenções do repo

- Beta: tag `v<core>-beta.N` (ex.: `v1.0.0-beta.4`), **no branch `beta`**.
  `<core>` vem de `project(kai VERSION x.y.z ...)` no `CMakeLists.txt`.
  Se o usuário disser só `beta.4`, a tag é `v<core>-beta.4`.
- Estável: tag `vX.Y.Z` no `main` (`release-stable.yml`).
- `release-beta.yml` dispara no push da tag e valida: formato, tag ancestral de
  `origin/beta`, versão igual ao CMake; depois testa, empacota
  (Windows zip + setup, AppImage) e publica o pre-release.
- Commits/PRs: sem `Co-Authored-By`, sem "Generated with" (AGENTS.md §11).
  Autor = `git config user.name/user.email`.
- Não rodar cmake/ctest/docker localmente: o usuário controla o container de
  build. A verificação é o CI do GitHub.

## Passos

1. **Auditoria**: `git status`, branches, `git log origin/beta..HEAD`, PRs
   abertos (`gh pr list`). Ver untracked sem lixo/segredos. Nunca commitar
   `build*/`, `dist/`, `.claude/worktrees`.
2. **Commits**: agrupar por assunto, mensagens claras (conventional commits).
3. **PR** da branch de trabalho para `beta` (`gh pr create --base beta`).
   Aguardar o CI inteiro: `gh pr checks <n> --watch`.
4. **Ciclo de correção do CI**: se falhar, ler o log (`gh run view --log-failed`),
   achar a causa raiz, corrigir (nunca desabilitar/pular teste), commit, push,
   esperar de novo. Repetir até tudo verde.
5. **Integração da versão publicada anterior**: conferir que a tag/branch da
   versão anterior está contida em `origin/beta`
   (`git merge-base --is-ancestor <anterior> origin/beta`). Se não estiver,
   abrir PR dela para `beta`, revisar o diff, esperar CI verde.
6. **Merge** dos PRs só com checks verdes e respeitando a política do repo
   (sem force push, sem reescrever histórico). Depois `git fetch` e confirmar
   que `origin/beta` é o commit validado.
7. **Tag**: no commit exato de `origin/beta`
   (`git tag <tag> origin/beta && git push origin <tag>`). Verificar com
   `git ls-remote --tags origin <tag>`.
8. **Release**: acompanhar `gh run watch` do workflow `Release (beta)` e
   confirmar `gh release view <tag>` com os artefatos + `SHA256SUMS`.
9. **Se a publicação da tag/release falhar**: pode apagar a tag
   (`git push origin :refs/tags/<tag>`, `git tag -d <tag>`), corrigir a causa,
   e recriar a tag. Se já existir release parcial, `gh release delete <tag>`
   antes de recriar. Apenas tags novas do lançamento em curso; nunca apagar
   tags de versões anteriores.
10. **Relatório final**: resumo, arquivos, commits (hashes), PRs (links),
    resultado de cada CI, problemas e soluções, branch final, tag + commit,
    confirmação do push, status da release. Sem alegar sucesso sem evidência.
