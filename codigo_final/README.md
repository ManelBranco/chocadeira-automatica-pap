# Código Final

Esta pasta contém a versão final e estável até ver do código da Chocadeira Automática.

Atenção antes da utilização do código disponibilizado o utilizador deve alterar os dados relativos á ligação á internet e ao Blynk (devendo utilizar uma rede 2.4GHZ para maximar a compatibilidade).

#define BLYNK_TEMPLATE_ID "ID do seu Template do Blynk"
#define BLYNK_TEMPLATE_NAME "Nome do seu Template do Blynk"
#define BLYNK_AUTH_TOKEN "Auth_Token do seu Dispositivo do Blynk"

// ===================== WIFI =====================

const char* WIFI_SSID = "Nome da Sua Rede";

const char* WIFI_PASS = "Password da Sua Rede";

WebServer server(80);


Inclui:

- Ficheiro principal do Arduino (.ino)
- Lógica completa de controlo automático
- Gestão de temperatura e humidade
- Sistema de viragem automática
- Comunicação com a aplicação móvel
- Bibliotecas necessárias ao funcionamento

Esta versão corresponde à implementação final apresentada no projeto, após testes, correções e otimizações.

O código encontra-se organizado e preparado para execução no sistema físico da chocadeira.

