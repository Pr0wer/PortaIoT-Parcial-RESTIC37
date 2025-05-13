#include <stdio.h>             
#include <string.h>             
#include <stdlib.h>

#include "pico/stdlib.h"         
#include "hardware/adc.h"   
#include "hardware/timer.h"     
#include "pico/cyw43_arch.h"     

#include "lwip/pbuf.h"           
#include "lwip/tcp.h"           
#include "lwip/netif.h"   

#include "lib/ssd1306.h"

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "MEU_SSID"
#define WIFI_PASSWORD "MINHA_SENHA"

// Tamanho da senha em caracteres
#define PASSWORD_SIZE 3

// Pinos
const uint led_green_pin = 11;
const uint led_red_pin = 13;

static const char *senha = "A1B"; // Senha definida para o programa
static char digitado[PASSWORD_SIZE + 1] = {' ', ' ', ' ', '\0'}; // Buffer para armazenar senhas digitadas pelo usuário
static uint8_t cursor = 0; // Cursor da posição atual da digitação

static bool displayUpdate = true; // Flag para atualizar o displaySSD1306 (Já começa ativado para inicialização)
static bool confirmou = false; // Indica se o programa está em processo de validação de uma senha

// Headers de função
void inicializarLed(void); // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void limparBuffer(int size, char array[size]); // Limpa um array de caracteres, preenchendo-o com espaços
int64_t led_result_callback(alarm_id_t id, void* user_data); // Callback para transitar do modo de validação ao de digitação
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err); // Função de callback ao aceitar conexões TCP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err); // Função de callback para processar requisições HTTP
void user_request(char **request); // Tratamento do request do usuário

// Função principal
int main()
{   
    // Inicializa o STDIO
    stdio_init_all();

    // Inicializa o display
    ssd1306_t ssd;
    ssd1306_i2c_init(&ssd);

    // Mostrar no display que o programa está inicializando
    ssd1306_draw_string(&ssd, "Iniciando", 48, 20); 
    ssd1306_send_data(&ssd);

    // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
    inicializarLed();

    //Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    //vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");

    while (true)
    {
        // Atualiza o display caso necessário
        if (displayUpdate)
        {   
            // Limpa o display
            ssd1306_fill(&ssd, false);

            // Desenha o menu
            ssd1306_draw_string(&ssd, "Senha: ", 40, 12);
            ssd1306_draw_string(&ssd, senha, 48, 20);
            ssd1306_draw_string(&ssd, "Digitado: ", 8, 36);
            ssd1306_draw_string(&ssd, digitado, 8, 44);

            // Envia as novas informações
            ssd1306_send_data(&ssd);

            // Atualiza a flag após a conclusão
            displayUpdate = false;
        }

        // Se o usuário confirmou a senha digitada e ela possui o tamanho correto
        if (cursor >= PASSWORD_SIZE && confirmou)
        {
            cursor = 0;

            // Verifica se a senha digitada foi correta
            if (strcmp(senha, digitado) == 0)
            {  
                // Indicar acerto pelo LED verde
                gpio_put(led_green_pin, 1);
            }
            else
            {  
                // Indicar erro pelo LED vermelho
                gpio_put(led_red_pin, 1);
            }

            // Alarme para desligar os LEDs após 1,5 segundos
            add_alarm_in_ms(1500, led_result_callback, NULL, false);
        }

        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);      // Reduz o uso da CPU
    }

    //Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void inicializarLed(void)
{
    // Configuração dos LEDs como saída
    gpio_init(led_green_pin);
    gpio_set_dir(led_green_pin, GPIO_OUT);
    gpio_put(led_green_pin, false);
    
    gpio_init(led_red_pin);
    gpio_set_dir(led_red_pin, GPIO_OUT);
    gpio_put(led_red_pin, false);
}

// Limpa um array de caracteres, preenchendo-o com espaços
void limparBuffer(int size, char array[size])
{
    for (int i = 0; i < size; i++)
    {   
        // Associar a posição atual com espaço se não for um caractere nulo
        if (!(i == size - 1))
        {
            array[i] = ' ';
        }
    }
}

// Callback que desliga os LEDs e finaliza o processo de validação de senha
int64_t led_result_callback(alarm_id_t id, void* user_data)
{
    if (gpio_get(led_green_pin))
    {
        gpio_put(led_green_pin, 0);
    }
    if (gpio_get(led_red_pin))
    {
        gpio_put(led_red_pin, 0);
    }

    limparBuffer(PASSWORD_SIZE + 1, digitado);
    displayUpdate = true;
    confirmou = false;
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário - digite aqui
void user_request(char **request)
{   
    // Requests de digitos
    // Associam o digito escolhido à posição atual do cursor e "pedem" a atualização do display
    if (cursor < PASSWORD_SIZE)
    {
        if (strstr(*request, "GET /digit_A") != NULL)
        {
            digitado[cursor] = 'A';
            cursor++;
            displayUpdate = true;
        }
        else if (strstr(*request, "GET /digit_B") != NULL)
        {
            digitado[cursor] = 'B';
            cursor++;
            displayUpdate = true;
        }
        else if (strstr(*request, "GET /digit_C") != NULL)
        {
            digitado[cursor] = 'C';
            cursor++;
            displayUpdate = true;
        }
        else if (strstr(*request, "GET /digit_1") != NULL)
        {
            digitado[cursor] = '1';
            cursor++;
            displayUpdate = true;
        }
        else if (strstr(*request, "GET /digit_2") != NULL)
        {
            digitado[cursor] = '2';
            cursor++;
            displayUpdate = true;
        }
        else if (strstr(*request, "GET /digit_3") != NULL)
        {
            digitado[cursor] = '3';
            cursor++;
            displayUpdate = true;
        }
    }
    
    // Requests de operações
    if (strstr(*request, "GET /confirm") != NULL && cursor >= PASSWORD_SIZE)
    {   
        // Atualiza o estado do programa para a validação de senha
        confirmou = true;
    }
    else if (strstr(*request, "GET /erase") != NULL)
    {  
        // Apaga todo o conteúdo do buffer digitado e pede atualização do display
        limparBuffer(PASSWORD_SIZE + 1, digitado);
        cursor = 0;
        displayUpdate = true;
    }
};

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    // Tratamento de request caso não se esteja validando a senha
    if (!confirmou)
    {
        user_request(&request);
    }

    // Cria a resposta HTML
    char html[2048];

    // Instruções html do webserver
    snprintf(html, sizeof(html), // Formatar uma string e armazená-la em um buffer de caracteres
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title> Embarcatech - Porta IoT </title>\n"
             "<style>\n"
             "body { background-color:rgb(49, 52, 53); font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }\n"
             "h1 { font-size: 64px; margin-bottom: 30px; }\n"
             "button { background-color: LightBlue; font-size: 36px; margin: 10px; padding: 20px 40px; border-radius: 10px; }\n"
             ".temperature { font-size: 48px; margin-top: 30px; color: #333; }\n"
             "</style>\n"
             "</head>\n"
             "<body>\n"
             "<h1>Embarcatech: Porta IoT</h1>\n"
             "<form action=\"./digit_A\"><button>A</button></form>\n"
             "<form action=\"./digit_B\"><button>B</button></form>\n"
             "<form action=\"./digit_C\"><button>C</button></form>\n"
             "<form action=\"./digit_1\"><button>1</button></form>\n"
             "<form action=\"./digit_2\"><button>2</button></form>\n"
             "<form action=\"./digit_3\"><button>3</button></form>\n"
             "<form action=\"./confirm\"><button>Confirmar</button></form>\n"
             "<form action=\"./erase\"><button>Apagar</button></form>\n"
             "</body>\n"
             "</html>\n");

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    //libera memória alocada dinamicamente
    free(request);
    
    //libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}
