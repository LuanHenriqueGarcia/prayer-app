# PrayerApp nativo (C++)

Reescrita do PrayerApp em C++/Win32: **um único `PrayerApp.exe` (~540 KB, sem DLL nem runtime extra)**
que fica na bandeja, sobe o WebSocket na porta `8080` e imprime. Usa ~20 MB de RAM (o Electron passava de 150 MB).

## O que faz (mesmo protocolo do Electron)

| Ação (`acao`) | Como imprime agora | Resposta |
|---|---|---|
| `todasImpressoras` | `EnumPrinters` (sem PowerShell) | `{status:"success", acao, data:[nomes]}` |
| `imprimir` (RAW: ZPL/EPL/ESC-POS) | spooler direto (`WritePrinter`, tipo RAW; `XPS_PASS` para driver v4); se falhar, grava no compartilhamento, como o antigo `copy /b` | `{status, message, acao:"imprimir"}` |
| `imprimirHTML` | WebView2 oculta (motor do Edge que já vem no Windows), impressão silenciosa sem margem | `{status, message, acao:"imprimirHTML"}` |
| `imprimirCupon` (`msg.content` = PDF base64) | `Windows.Data.Pdf` renderiza na resolução da impressora e ajusta à página (o antigo "fit") | `{status, message, acao:"imprimirPDF"}` |
| `debug/printers` | grava a lista no log | `{status:"ok", acao}` |

O campo da impressora é `impressora` (aceita também `impresora`, a grafia do README antigo).
Mensagens de erro, notificações e o log (`%TEMP%\prayer\prayer-AAAA-MM-DD.log`) seguem os mesmos textos do Electron.

Bandeja: versão/atualizar, iniciar e parar o servidor, ver logs, teste de impressão (duplo clique), pasta de logs,
"Abrir junto com o Windows" (marcável), sobre, sair.

## Instalação e atualização

- Rodar o `PrayerApp.exe` de qualquer lugar: ele se copia para `%LOCALAPPDATA%\PrayerApp\PrayerApp.exe`,
  cria o atalho no Menu Iniciar, registra o início com o Windows (`HKCU\...\Run\PrayerApp`) e reabre de lá. Não pede admin.
- Auto-update: 4 s depois de abrir (e pelo menu) consulta a última release de `LuanHenriqueGarcia/prayer-app`. Se for mais nova e
  tiver o asset `PrayerApp.exe`, baixa, **confere o SHA-256** (digest do GitHub ou `PrayerApp.exe.sha256`) e pergunta se instala.
  A troca é feita renomeando o exe em uso; a nova versão abre sozinha.
- `--no-install`: roda de onde está, sem se copiar, sem registro de início e sem auto-update (desenvolvimento).

## Compilar

Passo a passo completo (instalação em máquina nova, VS Code, depuração, problemas comuns): **[COMPILAR.md](COMPILAR.md)**.

Resumo: Visual Studio 2022 Build Tools com "Desenvolvimento para desktop com C++", depois `build.cmd` (Release, em
`build\PrayerApp.exe`) ou `build.cmd debug`. No VS Code: `Ctrl+Shift+B` compila, `F5` depura.

## Release

A versão fica no `CMakeLists.txt` (`project(PrayerApp VERSION x.y.z)`). A release precisa da tag `vx.y.z` com
`PrayerApp.exe` + `PrayerApp.exe.sha256` (como gerar: [COMPILAR.md](COMPILAR.md), passo 6). No repositório GitHub
`LuanHenriqueGarcia/prayer-app` isso é feito pelo workflow manual **Release (nativo C++)** (`.github/workflows/release-native.yml`).

## Diferenças em relação ao Electron

- Telas de Logs e Teste de Impressão são nativas do Windows (sem o tema escuro do Bootstrap).
- `imprimir` envia os bytes pela API do spooler, não mais por `copy /b` + `print.exe`.
- Travamentos ficam registrados no log (código + endereço) em vez de sumir.
