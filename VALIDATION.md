# Validação desta entrega

Data: 15/10/2026. Host: Linux x86-64. Não há instalação do CS2, desktop Windows ou MSVC neste host.

## Executado

- Configuração CMake e compilação do núcleo/testes com GCC 14.2.0 em modo Debug.
- `ctest --preset linux-tests --verbose`: **8/8 suítes, 100 verificações aprovadas**, nenhuma suíte pulada ou desabilitada.
- Compilação cruzada do aplicativo completo com MinGW-w64 GCC 14 (POSIX), Windows x64, em modo Release; DirectX 11, DirectComposition e backends Win32/DX11 do ImGui vinculados ao executável.
- Inspeção PE: executável PE32+ x86-64, imports de `d3d11.dll`, `D3DCOMPILER_47.dll`, `dcomp.dll` e APIs Windows. Runtime C++ MinGW ligado estaticamente no aplicativo.
- Perfil de offsets e campos derivado dos arquivos do commit fixo `f0c84252829602347757ecbb0810d74d3c837a4b` de `a2x/cs2-dumper`. O dump declara build interna `14191` e timestamp `2026-10-15T13:10:38.711921300+00:00`.

| Suíte | Verificações | Cobertura |
|---|---:|---|
| math | 18 | Projeção e clipping, ângulos, suavização temporal, alvo/time/vida/FOV |
| pattern | 8 | Wildcards, rejeição de assinaturas inválidas/ambíguas, RIP positivo/negativo |
| geometry | 11 | OBJ, segmentos bloqueados/livres, mapa incorreto, BVH, alvo ocluído |
| config | 15 | JSON roundtrip/substituição, migração dos arquivos antigos/hotkeys, novas opções e valores inválidos |
| schema | 7 | Campos dinâmicos, ciclos/limites, offset zero, export e registro de interface |
| profile | 4 | Build e valores do dump, rejeição de build incompatível |
| entities | 14 | Frame remoto sintético, nome/arma/bones, serial reciclado, dormência, respawn e índice sob a mira |
| trigger | 23 | Atraso, intervalo, cancelamento, troca de alvo, filtros de vida/time/visibilidade e entidade desconhecida |

Os fixtures de memória testam o comportamento do leitor em uma representação conhecida. Eles **não** demonstram que as estruturas privadas do CS2 instalado têm aquele layout.

## Reprodução dos comandos neste workspace

As ferramentas foram instaladas fora do checkout, em `/workspace/.lsdware-tools`, `/workspace/.lsdware-toolchain` e `/workspace/.lsdware-format`. As dependências CMake estão em `build/*/_deps` e os outputs são ignorados pelo Git.

```sh
/workspace/.lsdware-tools/cmake/data/bin/cmake --preset linux-tests \
  -DCMAKE_MAKE_PROGRAM=/workspace/.lsdware-tools/bin/ninja
/workspace/.lsdware-tools/cmake/data/bin/cmake --build --preset linux-tests --parallel 2
/workspace/.lsdware-tools/cmake/data/bin/ctest --preset linux-tests --verbose

/workspace/.lsdware-tools/cmake/data/bin/cmake -S . -B build/mingw -G Ninja \
  -DCMAKE_MAKE_PROGRAM=/workspace/.lsdware-tools/bin/ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=/workspace/.lsdware-toolchain/usr/bin/x86_64-w64-mingw32-g++-posix \
  -DCMAKE_RC_COMPILER=/workspace/.lsdware-toolchain/usr/bin/x86_64-w64-mingw32-windres \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
/workspace/.lsdware-tools/cmake/data/bin/cmake --build build/mingw --parallel 2
```

No Windows, use os comandos MSVC do `README.md`, não esses caminhos locais Linux.

## Não executado / não confirmado

- Compilação com MSVC; o preset e o workflow Windows foram preparados, mas não executados aqui.
- Execução do binário Windows, inclusive demonstração, transparência, alinhamento DPI, hotkeys e recuperação de falhas gráficas.
- Aceitação dos cliques `SendInput` pelo CS2, leitura real de `m_iIDEntIndex` e soltura do botão sob mudanças de foco no Windows.
- Leitura/escrita de memória de um processo CS2 real ou efeito de `dwViewAngles` sobre a câmera. A execução normal agora pede acesso de escrita; `--read-only` preserva o modo somente leitura.
- Correspondência entre o identificador fornecido `25966348` e a build interna `14191`. A execução pode conferir o primeiro no manifesto Steam e exige que o segundo corresponda ao perfil.
- Stride de entidades, identidade/serial de handles, buffer e índices de bones por modelo, nome de mapa e layout ABI do schema na instalação do usuário.
- Correspondência de um OBJ de colisão real com a revisão do mapa instalado; somente uma parede sintética foi carregada nos testes.
- Resultado atual do GitHub Actions: não confirmado; a consulta à API de runs retornou `Forbidden`. A compilação e os testes acima foram executados no workspace.

O trace interno não é parte da arquitetura final escolhida. O raycast externo não modela elementos dinâmicos nem materiais do jogo. O código exibe diagnósticos e recusa conexão em build interna incompatível; esses mecanismos não substituem a validação no jogo.

## Correção do modo somente leitura

A atualização foi aplicada sobre `a352b7b`, que já contém o painel de diagnóstico integrado pelo PR #1. A causa identificada foi `Options::write` iniciar como `false`, exigindo `--write-angles` mesmo quando a assistência estava habilitada no menu.

A execução normal agora solicita acesso de escrita; `--read-only` o desativa explicitamente. O painel usa a permissão da conexão real e registra a quantidade de escritas concluídas e o último erro do Windows. Os pacotes de código e executável em `dist/` foram regenerados para incluir essa correção e o painel existente.

Após a correção: compilação cruzada Windows x64 aprovada e 7/7 suítes do núcleo aprovadas, com 67 verificações. Ainda não foi possível executar a correção contra o CS2 neste host Linux; acesso de escrita e chamadas bem-sucedidas não comprovam que a engine aplicou os ângulos.

## Opções de exibição e triggerbot

Adicionadas opções persistentes para mostrar o FOV e o painel de diagnóstico, ambas desligadas inicialmente, com carregamento retrocompatível de JSONs existentes. Adicionado triggerbot independente, com Mouse 5, atraso/intervalo configuráveis e seleção pela entidade sob a mira. O campo `m_iIDEntIndex` (14044) foi obtido do dump original após conferência de seu SHA-256. O modo de demonstração simula cliques; o modo real usa a API Windows `SendInput`.

Nesta atualização, os testes passaram para 8 suítes e 100 verificações, e o executável completo foi recompilado para Windows x64 com MinGW. Os testes exercitam o núcleo e fixtures de memória; não são evidência de disparos dentro do jogo. Os pacotes em `dist/` incluem a atualização.

## Exibição de LOS e marcador do alvo

Adicionados controles independentes para os textos de LOS e a bolinha verde, desligados por padrão. A persistência e o carregamento de JSONs antigos estão cobertos pelas verificações existentes de configuração. As opções condicionam somente a renderização desses elementos; não alteram os filtros de alvo ou de visibilidade. A compilação Windows x64 e as 8 suítes (100 verificações) foram repetidas após essa alteração. A inspeção visual no Windows permanece pendente.
