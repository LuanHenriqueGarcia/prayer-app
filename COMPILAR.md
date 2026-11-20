# Como compilar o PrayerApp nativo (C++) em outro computador

Guia para quem vai dar manutenção. Resultado: um único `PrayerApp.exe`, sem DLL nem runtime para instalar na loja.
O que o app faz está em [README.md](README.md).

---

## 1. Instalar o que precisa (uma vez por computador)

Windows 10 ou 11, 64 bits.

### 1.1 Visual Studio 2022 Build Tools (compilador C++)

Só o compilador; não precisa do Visual Studio completo. No PowerShell:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Ou pelo instalador (https://visualstudio.microsoft.com/pt-br/downloads/ → "Ferramentas de Build para Visual Studio 2022"),
marcando a carga de trabalho **"Desenvolvimento para desktop com C++"**. Os componentes recomendados já trazem tudo:

| Componente | Para quê |
|---|---|
| MSVC v143 (x64/x86) | compilador |
| Windows 11 SDK (10.0.22621 ou mais novo) | headers do Windows, C++/WinRT (PDF) |
| Ferramentas do CMake do C++ para Windows | `cmake` e `ninja` |

> Já tem o Visual Studio 2022 Community/Professional com essa carga de trabalho? Serve igual.

### 1.2 VS Code (para editar e depurar)

```powershell
winget install --id Microsoft.VisualStudioCode
```

Extensão: **C/C++** (`ms-vscode.cpptools`). O VS Code sugere sozinho ao abrir a pasta.

### 1.3 Opcional

- **Git** (`winget install --id Git.Git`) para clonar/subir.
- **Python + Pillow** só se for trocar o ícone (passo 6).

Internet na **primeira** compilação: o CMake baixa o SDK do WebView2 do NuGet (versão fixa, conferida por SHA-256).
Depois fica em cache dentro de `build\`.

---

## 2. Compilar pela linha de comando

Na pasta do projeto (onde está o `CMakeLists.txt`):

```bat
build.cmd            :: Release  -> build\PrayerApp.exe        (o que vai para as lojas)
build.cmd debug      :: Debug    -> build-debug\PrayerApp.exe  (para depurar)
build.cmd clean      :: apaga build\ e build-debug\
```

O `build.cmd` acha o Visual Studio sozinho (via `vswhere`), abre o ambiente do compilador e roda o CMake + Ninja.
Funciona no `cmd`, no PowerShell (`.\build.cmd`) e com duplo clique.

Para testar sem instalar nada na máquina:

```bat
build\PrayerApp.exe --no-install
```

`--no-install` = roda dali mesmo, sem se copiar para `%LOCALAPPDATA%`, sem registrar início com o Windows e sem auto-update.
Sem esse parâmetro o exe se instala (é o comportamento da loja).

---

## 3. Trabalhar no VS Code

1. **Arquivo → Abrir Pasta...** e escolher a pasta do projeto (a que tem o `CMakeLists.txt` e a pasta `.vscode`).
2. Aceitar instalar a extensão **C/C++** se ele pedir.
3. **Compilar:** `Ctrl+Shift+B` (tarefa "Compilar Debug"). Erros aparecem na aba **Problemas** e o clique leva à linha.
   Outras tarefas em **Terminal → Executar Tarefa...**: "Compilar Release" e "Limpar".
4. **Rodar e depurar:** `F5` (configuração "PrayerApp (Debug)"). Compila, abre o app com `--no-install` e para nos breakpoints
   (clique à esquerda do número da linha). `Shift+F5` encerra.
   - Bons lugares para breakpoint: `src/actions.cpp` → `handleAction` (toda requisição do WebSocket passa por ali),
     `src/printers.cpp` → `printRaw`, `src/htmlprint.cpp` → `startPrint`, `src/pdfprint.cpp` → `printPdf`.
5. **IntelliSense** (autocompletar, ir para definição com `F12`): funciona depois da primeira "Compilar Debug",
   que gera `build-debug\compile_commands.json`. Se aparecer sublinhado vermelho em `#include`, compile uma vez e
   rode **C/C++: Reset IntelliSense Database** (`Ctrl+Shift+P`).

Os arquivos de configuração ficam em `.vscode\` (`tasks.json`, `launch.json`, `settings.json`) e vão junto no git.

> A extensão **CMake Tools** não é necessária. Se estiver instalada, ela pode perguntar por "kit"; pode ignorar
> (o `settings.json` já desliga a configuração automática dela).

---

## 4. Testar o servidor sem impressora

Com o app rodando, abrir o console do navegador (F12) em qualquer página e colar:

```javascript
const ws = new WebSocket('ws://127.0.0.1:8080');
ws.onmessage = (e) => console.log(JSON.parse(e.data));
ws.onopen = () => ws.send(JSON.stringify({ acao: 'todasImpressoras' }));
```

Deve voltar `{status: "success", acao: "todasImpressoras", data: [...]}`. O resto das ações está no README.
Para imprimir sem papel, use a tela **Teste de impressão** (duplo clique no ícone) com "Microsoft Print to PDF".

Log de tudo: `%TEMP%\prayer\prayer-AAAA-MM-DD.log` (menu do ícone → "Ver logs" ou "Pasta de logs").

---

## 5. Mapa do código

```text
CMakeLists.txt        build (versão do app na linha project(...), SDK WebView2, libs do Windows)
build.cmd             compila (Release/Debug/clean)
.vscode/              tarefas, depuração e IntelliSense do VS Code
res/                  ícone, manifesto, recursos de versão do .exe
src/
  main.cpp            entrada: instância única, auto-instalação, ícone da bandeja e menu, notificações
  wsserver.cpp        servidor WebSocket na porta 8080 (handshake, frames, uma thread por conexão)
  actions.cpp         ações do protocolo: todasImpressoras, imprimir, imprimirHTML, imprimirCupon, debug/printers
  printers.cpp        lista de impressoras, resolução do nome, envio RAW ao spooler
  htmlprint.cpp       impressão de HTML pela WebView2 oculta (fila, roda na thread da interface)
  pdfprint.cpp        impressão de PDF (Windows.Data.Pdf -> bitmap -> GDI)
  updater.cpp         auto-update pelo GitHub Releases (confere SHA-256)
  logviewer.cpp       tela "Ver logs"
  tester.cpp          tela "Teste de impressão"
  logger.cpp          log em %TEMP%\prayer
  json.cpp            leitor/gerador de JSON
  util.cpp            UTF-8, base64, SHA, caminhos
```

Regras que valem para mexer:
- Texto do usuário e do log em português; código e comentários em inglês.
- Nada de bloquear a thread da interface (`main.cpp`): trabalho demorado vai para thread e volta por `PostMessage`.
- Nova ação no WebSocket: só em `actions.cpp` → `handleAction`, devolvendo um `Json` com `status`.

---

## 6. Versão, ícone e release

- **Versão:** `CMakeLists.txt`, linha `project(PrayerApp VERSION 2.0.0 ...)`. Ela vai para o .exe, para "Sobre" e para o auto-update.
- **Ícone:** trocar `res\print.png` e rodar `python res\make-icon.py` (gera `res\app.ico`).
- **Auto-update:** o app instalado procura a última release de `LuanHenriqueGarcia/prayer-app` no GitHub e só atualiza se ela
  tiver os arquivos `PrayerApp.exe` e `PrayerApp.exe.sha256` e a tag `vX.Y.Z` for maior que a versão dele.
  Para publicar: subir a versão no `CMakeLists.txt`, compilar Release e anexar os dois arquivos na release.
  O `.sha256` se gera assim:
  ```powershell
  $h = (Get-FileHash build\PrayerApp.exe -Algorithm SHA256).Hash.ToLower()
  Set-Content PrayerApp.exe.sha256 "$h  PrayerApp.exe" -NoNewline -Encoding ascii
  ```
  Mudou o repositório das releases? Trocar `REPO_PATH` em `src/updater.cpp`.

---

## 7. Problemas comuns

| Sintoma | Causa / solução |
|---|---|
| `Visual Studio 2022 / Build Tools com C++ nao encontrado` | Passo 1.1 não feito, ou faltou a carga "Desenvolvimento para desktop com C++". |
| `cmake nao encontrado` / `ninja nao encontrado` | Falta o componente "Ferramentas do CMake do C++ para Windows" (Visual Studio Installer → Modificar). |
| `SSL peer certificate ... was not OK` na 1ª compilação | Rede com inspeção de TLS. O `CMakeLists.txt` já tenta de novo pelo PowerShell; se falhar também, baixar à mão `https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4022.49/microsoft.web.webview2.1.0.4022.49.nupkg` e salvar como `build\webview2-1.0.4022.49.nupkg`. |
| `WebView2 SDK hash mismatch` | Download corrompido/interceptado: `build.cmd clean` e compilar de novo. |
| `Cannot open include file: 'winrt/...'` | Windows SDK ausente ou antigo: instalar o Windows 11 SDK pelo Visual Studio Installer. |
| App avisa "porta 8080 ocupada" | Outro PrayerApp (inclusive a versão Electron) está aberto. Fechar pelo ícone da bandeja. |
| HTML não imprime: "WebView2 Runtime indisponível" | PC sem o Microsoft Edge WebView2 Runtime (raro no Win10/11 atualizado): instalar em https://developer.microsoft.com/microsoft-edge/webview2/. |
| Mudou `CMakeLists.txt` e o build estranhou | `build.cmd clean` e compilar de novo. |
| App fechou sozinho | Ver o log do dia: travamentos ficam registrados com "PrayerApp travou". |
