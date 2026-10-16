# LSDWare — treino visual externo para CS2

Aplicativo C++20 para Windows 10/11 x64, DirectX 11, DirectComposition e Dear ImGui. O processo externo lê entidades, desenha informações e oferece assistência de mira por escrita de ângulos. A conexão permite escrita por padrão; a assistência é ativada no menu e executada enquanto a hotkey estiver pressionada. Há também um modo de demonstração independente do CS2.

**Estado da integração:** o código não foi executado contra o CS2 nesta máquina Linux. O perfil incluído usa o dump comunitário da build interna **14191**, publicado em **15/10/2026**, e não equivale a uma certificação de compatibilidade com uma instalação do jogo. O BuildID informado, **25966348**, pode ser conferido separadamente no manifesto Steam pelo parâmetro `--steam-build`. Não foi estabelecida neste ambiente a correspondência entre esse BuildID e a build interna 14191.

## Compilação no Windows

Instale Visual Studio 2022 com **Desenvolvimento para desktop com C++**, MSVC v143, Windows 10/11 SDK, CMake 3.24+ e Git. No terminal, na raiz do checkout:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc --parallel 2
ctest --preset windows-msvc
.\build\windows\Release\lsdware.exe --demo
```

O CMake obtém ImGui 1.91.9b e nlohmann/json 3.12.0 por commits fixos. É necessário acesso HTTPS ao GitHub na primeira configuração. Não há bibliotecas Source 1, drivers nem dependência de uma DLL injetada. A distribuição precisa conter `lsdware.exe` e o diretório `data` que o build copia ao lado dele. Para outro Visual Studio, use um gerador CMake instalado que suporte x64.

## Execução com CS2

Use o jogo em janela ou janela sem bordas; o overlay composto não suporta fullscreen exclusivo. Entre em um mapa de treino e execute:

```powershell
# Execução normal: leitura e escrita de ângulos disponíveis para o AIM.
.\build\windows\Release\lsdware.exe --steam-build 25966348

# Opcional: somente leitura, sem escrita no processo.
.\build\windows\Release\lsdware.exe --steam-build 25966348 --read-only
```

O aplicativo procura `cs2.exe`, lê o BuildID em `steamapps/appmanifest_730.acf` e compara com `--steam-build` quando fornecido. Independentemente dessa opção, identifica a build interna por assinatura em `engine2.dll`. Uma build interna diferente do perfil, ou uma assinatura de build ausente, impede a conexão. `OpenProcess` negado é exibido como erro; não há mecanismo de contorno.

O menu inicia aberto. Ative `Enable assistance while hotkey held` e feche o menu com **Insert**, pois a assistência só funciona com o jogo em primeiro plano e o menu fechado. Segure **Mouse 4** para atuar sobre um alvo elegível. A configuração inicial exige visibilidade: sem um OBJ válido do mapa atual, nenhum alvo é liberado.

Não é mais necessário passar `--write-angles`; abrir o executável normalmente pede acesso de leitura e escrita. A opção antiga continua aceita. `--read-only` desliga a escrita explicitamente. O painel `Aim diagnostics` mostra `WRITE ENABLED` somente quando existe uma conexão com permissão de escrita, `READ ONLY` para uma conexão somente leitura e `NOT CONNECTED` enquanto não há conexão. `Successful angle writes` conta chamadas de escrita concluídas desde a última conexão; não confirma que o jogo aplicou os ângulos. Falhas mostram o código de erro do Windows. O checkbox de assistência, a hotkey, o FOV, os bones válidos e o filtro de visibilidade continuam sendo condições necessárias.

| Ação | Padrão | Código VK decimal |
|---|---|---:|
| Abrir/fechar menu | Insert | 45 |
| Segurar assistência | Mouse 4 / XBUTTON1 | 5 |
| Segurar triggerbot | Mouse 5 / XBUTTON2 | 6 |
| Encerrar aplicativo | End | 35 |

As teclas podem ser alteradas no menu, por código virtual do Windows, usando `Apply keys`. O triggerbot pode compartilhar a tecla da assistência, mas não pode usar o botão esquerdo, a tecla de menu ou a de encerramento. `Save JSON` salva `config.json` ao lado do executável; `Load JSON` recarrega uma configuração validada. `--config arquivo.json` e `--profile arquivo.json` permitem caminhos alternativos. Caminhos relativos de colisão são resolvidos a partir do diretório do JSON de configuração. O comando de gravação usa arquivo temporário e substituição do arquivo de destino.

O FOV é o **meio-ângulo do cone em graus**, não um raio em pixels. A suavização é uma constante de tempo em segundos: `0` aplica a direção imediatamente; `0.15` converge gradualmente, independentemente da taxa de quadros. Cabeça, pescoço e peito usam os índices do perfil. Alvos aliados, mortos, com dados inválidos ou fora do cone são descartados. A escrita é limitada ao endereço de ângulos resolvido para o perfil, com normalização, conferência do pawn local e dados da iteração atual.

Na seção `Aim training`, `Show FOV circle` mostra/oculta o desenho do cone e `Show aim diagnostics` mostra/oculta o painel de diagnóstico. **Ambos ficam desligados por padrão.** Ocultar o círculo não altera a seleção por FOV. Essas preferências são salvas no JSON; arquivos antigos continuam válidos e recebem os dois valores desligados.

Na seção `Visual`, `Show LOS labels` mostra/oculta os textos `LOS unknown`, `LOS static` e `Blocked`. `Show target marker (green)` controla separadamente a bolinha verde do alvo selecionado. **Ambos ficam desligados por padrão**, inclusive ao carregar um JSON antigo sem essas opções; use `Save JSON` para guardar suas preferências. Essas opções alteram apenas o desenho: a seleção do AIM, o triggerbot e a checagem de visibilidade continuam funcionando conforme suas configurações.

### Triggerbot

Marque `Enable triggerbot while hotkey held`, feche o menu e segure **Mouse 5**. Ele é independente do checkbox da assistência: usa a entidade sob a mira, lida de `C_CSPlayerPawn::m_iIDEntIndex`, e não dispara apenas porque um alvo entrou no FOV da assistência.

- Inicia desligado. Atraso padrão: **80 ms**; intervalo entre cliques: **150 ms**, ambos configuráveis.
- Exige jogador local vivo, inimigo vivo sob a mira, menu fechado, jogo em primeiro plano, hotkey pressionada e dados recentes. Trocar de alvo ou perder uma condição reinicia a espera.
- Compartilha `Require static geometry visibility`: quando ativo, exige LOS estática até o osso selecionado. Essa checagem tem as mesmas limitações do OBJ descritas abaixo.
- Usa `SendInput` para pressionar o botão esquerdo e soltá-lo após aproximadamente 30 ms, na próxima iteração disponível. Também solicita soltura ao perder ativação, abrir o menu, perder o foco ou encerrar. Não inicia um clique enquanto o botão esquerdo já está pressionado.
- `--read-only` também desativa os cliques automáticos. `--demo` apenas incrementa o contador de cliques simulados, com volumes sintéticos ao redor dos bones; não injeta cliques.
- `Submitted clicks` conta solicitações enviadas ao Windows, não disparos ou acertos confirmados pelo jogo. A precisão temporal depende dos frames. Armas automáticas podem produzir mais de um disparo durante uma pressão; não há sincronização com munição ou cadência da arma.

O offset do campo sob a mira foi conferido no mesmo dump fixo do perfil. Sua leitura no jogo e a aceitação de `SendInput` pelo CS2 ainda precisam de teste no Windows. Falhas de entrada desabilitam o triggerbot e aparecem no menu.

## Colisão e visibilidade

Conforme a arquitetura external escolhida, há um **raycast geométrico real**, por segmentos contra triângulos e uma BVH. Não há chamada ao trace interno do Source 2. `ReadProcessMemory` não executa métodos da engine em outro processo; endereços de interfaces e vtables remotos não podem ser chamados diretamente como ponteiros locais.

Forneça um OBJ do **mesh de colisão**, exportado por uma ferramenta compatível com a versão do mapa. O projeto não extrai automaticamente VPK/VPHYS e não inclui mapas do jogo. Requisitos do arquivo:

- coordenadas mundiais Source, eixo Z para cima, unidades originais, sem reposicionar, girar ou escalar o mapa;
- vértices `v` e faces `f` trianguladas; também são aceitas faces convexas, trianguladas em leque, índices negativos e referências `v/vt/vn`;
- somente superfícies que devem bloquear a linha de visão; materiais, UVs e normais são ignorados;
- nome exato do mapa em `Map name`, por exemplo `de_dust2`, correspondente ao nome observado em `Game map`.

Preencha `OBJ file` e `Map name`, clique `Load collision` e depois `Save JSON`. Se o arquivo não existe, é inválido, não corresponde ao mapa ou o nome remoto está indisponível, a visibilidade permanece **desconhecida** e a assistência com filtro de visibilidade não seleciona o alvo. Um erro de carregamento preserva a configuração/geometria anterior.

A geometria estática não incorpora portas móveis, alterações destrutíveis, jogadores, fumaça, materiais translúcidos, máscaras de colisão ou o estado dinâmico da partida. Portanto `LOS static` significa apenas que o segmento não encontrou um triângulo no arquivo fornecido. A interface distingue `LOS static`, `Blocked` e `LOS unknown`. A precisão depende do conteúdo e da revisão do mapa exportado.

`data/training.obj` é uma parede sintética, usada apenas por `--demo` e pelos testes; não representa um mapa CS2. Na demonstração há cinco alvos móveis, cores por time, nomes, vida, distância, arma e uma parede que bloqueia o raycast. Com o menu fechado, mova o mouse para alterar a câmera, habilite a assistência e segure Mouse 4. `Reset demo view` restaura a direção inicial. A latência exibida mede a entrada de um alvo elegível no cone até a hotkey, não confirma disparos ou acertos no jogo.

## Source 2: o que está implementado e o que falta verificar

| Componente | Implementação | Limite de validação |
|---|---|---|
| Memória | Toolhelp32, OpenProcess, ReadProcessMemory e WriteProcessMemory | Precisa de execução Windows contra o jogo |
| Pattern scanning | Seções executáveis PE, wildcards, deslocamentos RIP relativos com sinal; rejeita múltiplas ocorrências | Assinaturas derivadas do snapshot comunitário |
| Interfaces | Export PE `CreateInterface`, lista `InterfaceReg`, factories LEA/MOV reconhecidas | Sem executar factories ou vtables remotas; prólogos diferentes são rejeitados |
| Schema dinâmico | `SchemaSystem_001`, scopes de `client.dll`, classes e campos de `UtlTsHash` | Estrutura do parser depende do ABI; fallback explícito por build |
| Entity list | Blocos de 512, handles com comparação de serial, controllers/pawns | Stride e layout de identity precisam de confirmação no jogo |
| Bones | Posições em model state; cabeça/pescoço/peito configurados no perfil | Layout do buffer e índices não são fornecidos pelo schema refletido; precisam de confirmação por modelo/build |
| Visibilidade | Segmento/triângulos com BVH externa | Só a geometria estática exportada; não usa trace interno |
| Overlay | DX11/DirectComposition com transparência premultiplicada, ImGui, DPI e alinhamento da área cliente | Precisa de teste visual em Windows |

O perfil `data/cs2-14191.json` inclui RVAs e campos refletidos de [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper/tree/f0c84252829602347757ecbb0810d74d3c837a4b), com commit, timestamp e SHA-256 dos arquivos de origem. A licença MIT da origem está em `licenses/cs2-dumper-MIT.txt`. Não se trata de um SDK oficial completo do CS2: as estruturas comunitárias utilizadas estão explicitamente separadas das APIs Windows reais.

O parser usa schema em memória quando todos os campos consumidos estão disponíveis; caso contrário, usa o conjunto fixo do perfil e mostra o motivo em `Offset diagnostics`. Entity list, controller, matriz e globals tentam assinaturas primeiro. View angles usa o RVA do snapshot. **O fallback nunca é autorizado para uma build interna diferente.** O perfil também identifica os campos estruturais não verificados: lista de entidades, buffer/índices de bones e posição do nome de mapa em globals. Não basta alterar o número de build do JSON para atualizar a integração.

Para uma nova build, gere/obtenha um dump correspondente, compare as assinaturas e o ABI das estruturas não refletidas, atualize o perfil e teste primeiro com `--read-only`. Verifique nomes, vida, time, arma, projeção, bones, mapa e oclusão antes de testar a escrita. O programa não baixa offsets mutáveis na inicialização.

## Estrutura e testes

- `core`: projeção, ângulos, seleção de alvo, suavização, scanner e BVH/OBJ;
- `source2`: parser PE/interfaces/schema e amostragem externa independente das APIs Win32;
- `process_win`: processo e memória Windows;
- `overlay_win`: janela e renderização DirectX/DirectComposition;
- `main`: integração, menu, hotkeys, demonstração e escrita opcional;
- `config`: JSON validado e gravação por substituição;
- `tests`: testes executáveis, inclusive memória sintética para schema e interfaces.

No Linux, apenas núcleo e testes são nativos:

```sh
cmake --preset linux-tests
cmake --build --preset linux-tests --parallel 2
ctest --preset linux-tests
```

O workflow `.github/workflows/build.yml` prepara build e testes com MSVC em Windows e testes nativos Linux quando enviado ao GitHub. Sua presença não significa que esse CI foi executado. Veja `VALIDATION.md` para o registro dos comandos e resultados efetivamente obtidos nesta entrega.
