# Validação desta entrega

Data: 15/10/2026. Host: Linux x86-64. Não há instalação do CS2, desktop Windows ou MSVC neste host.

## Executado

- Configuração CMake e compilação do núcleo/testes com GCC 14.2.0 em modo Debug.
- `ctest --preset linux-tests --verbose`: **7/7 suítes, 67 verificações aprovadas**, nenhuma suíte pulada ou desabilitada.
- Compilação cruzada do aplicativo completo com MinGW-w64 GCC 14 (POSIX), Windows x64, em modo Release; DirectX 11, DirectComposition e backends Win32/DX11 do ImGui vinculados ao executável.
- Inspeção PE: executável PE32+ x86-64, imports de `d3d11.dll`, `D3DCOMPILER_47.dll`, `dcomp.dll` e APIs Windows. Runtime C++ MinGW ligado estaticamente no aplicativo.
- Perfil de offsets e campos derivado dos arquivos do commit fixo `f0c84252829602347757ecbb0810d74d3c837a4b` de `a2x/cs2-dumper`. O dump declara build interna `14191` e timestamp `2026-10-15T13:10:38.711921300+00:00`.

| Suíte | Verificações | Cobertura |
|---|---:|---|
| math | 18 | Projeção e clipping, ângulos, suavização temporal, alvo/time/vida/FOV |
| pattern | 8 | Wildcards, rejeição de assinaturas inválidas/ambíguas, RIP positivo/negativo |
| geometry | 11 | OBJ, segmentos bloqueados/livres, mapa incorreto, BVH, alvo ocluído |
| config | 7 | JSON roundtrip/substituição e rejeição de valores/configurações inválidos |
| schema | 7 | Campos dinâmicos, ciclos/limites, offset zero, export e registro de interface |
| profile | 4 | Build e valores do dump, rejeição de build incompatível |
| entities | 12 | Frame remoto sintético, nome/arma/bones, serial reciclado, dormência e respawn |

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
- Leitura/escrita de memória de um processo CS2 real ou efeito de `dwViewAngles` sobre a câmera.
- Correspondência entre o identificador fornecido `25966348` e a build interna `14191`. A execução pode conferir o primeiro no manifesto Steam e exige que o segundo corresponda ao perfil.
- Stride de entidades, identidade/serial de handles, buffer e índices de bones por modelo, nome de mapa e layout ABI do schema na instalação do usuário.
- Correspondência de um OBJ de colisão real com a revisão do mapa instalado; somente uma parede sintética foi carregada nos testes.
- Workflow GitHub Actions: arquivo criado, sem push ou execução remota nesta tarefa.

O trace interno não é parte da arquitetura final escolhida. O raycast externo não modela elementos dinâmicos nem materiais do jogo. O código exibe diagnósticos e recusa conexão em build interna incompatível; esses mecanismos não substituem a validação no jogo.
