/* 
Autore: Fabbri Simone, Lombardi Michele
Data: 08/10/2024
Titolo: Condizionatore di segnale per progetto ICAM
Descrizione: Si implementa un condizionatore di segnale di 4 celle di carico in grado di condividere i dati via Modbus e che intergisca con un PLC
Ultime modifiche: 
-riga 220, int32_t;
->La frequenza di campionamento misurata è di 5Hz.
Comandi Modpoll utili:
-dyrectory: cd C:\Users\fabbr\Downloads\modpoll-3.10\win
-richiesta dati: modpoll -b 9600 -p none -m rtu -a 1 -r 1 -c 13 COM4
-scrittura pesoCalib: modpoll -b 9600 -p none -m rtu -a 1 -r 12 COM4 5000
-richiesta tara: modpoll -b 9600 -p none -m rtu -a 1 -r 1 -t 0 COM4 1
-richiesta calibrazione: modpoll -b 9600 -p none -m rtu -a 1 -r 9 -t 0 COM4 1
*/

//INCLUSIONE LIBRERIE
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "mbcontroller.h"       // for mbcontroller defines and api
#include "modbus_params.h"      // for modbus parameters structures
#include "esp_log.h"            // for log_write
#include "sdkconfig.h"
#include "DAISY_PINOUT.hpp"
#include "HX711.hpp"
#include "COND.hpp"
#include "ALGO.hpp"
#include "PERSISTENCE.hpp"

#define VERSIONE 205 //v2.0.5
#define sample_freq 5 //Sample frequency in Hertz
#define times 5 //Numero medie per singole misure (tara e calibrazione * 2)
#define buffer 10   //Numero di campioni per filtri interni celle di carico
#define wait_time 1    //Numero di secondi di attesa per misura finale
#define discharge_time 1   //Numero di secondi oltre i quali siamo certi che il carrello sia stato scaricato
#define err_perc 10 //Distaccamento percentuale dalla misura di riferimento per il controllo di corretto funzionamento delle celle
static const char *SLAVE_TAG = "DAISY_COND_ICAM";
static portMUX_TYPE param_lock = portMUX_INITIALIZER_UNLOCKED;  //?????
int err=0;
//#define DIAGNOSTIC_MODBUS

extern "C" void app_main(void)
{
    init_Daisy();
    //-----------------------------SETUP MODBUS-----------------------------------------------------------------------------------------
    //mb_param_info_t reg_info; // keeps the Modbus registers access information
    mb_communication_info_t comm_info; // Modbus communication parameters
    mb_register_area_descriptor_t reg_area; // Modbus register area descriptor structure
    // Set UART log level
    esp_log_level_set(SLAVE_TAG, ESP_LOG_INFO);
    void* mbc_slave_handler = NULL;
    ESP_ERROR_CHECK(mbc_slave_init(MB_PORT_SERIAL_SLAVE, &mbc_slave_handler)); // Initialization of Modbus controller
    //SETUP COMMUNICATION PARAMETERS
    comm_info.mode = MB_MODE_RTU,
    comm_info.slave_addr = MB_SLAVE_ADDR;
    comm_info.port = MB_PORT_NUM;
    comm_info.baudrate = MB_DEV_SPEED;
    comm_info.parity = MB_PARITY_NONE;
    ESP_ERROR_CHECK(mbc_slave_setup((void*)&comm_info));
    //SETUP REGISTER AREA
    reg_area.type = MB_PARAM_HOLDING; // Set type of register area
    reg_area.start_offset = MB_REG_HOLDING_START; // Offset of register area in Modbus protocol
    reg_area.address = (void*)&holding_reg_params.holding_cell1MS; // Set pointer to storage instance
    reg_area.size = sizeof(holding_reg_params);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    // Initialization of Coils register area
    reg_area.type = MB_PARAM_COIL;
    reg_area.start_offset = MB_REG_COILS_START;
    reg_area.address = (void*)&coil_reg_params;
    reg_area.size = sizeof(coil_reg_params);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));
    // Starts of modbus controller and stack
    ESP_ERROR_CHECK(mbc_slave_start());
    //UART SETTING
    ESP_ERROR_CHECK(uart_set_pin(MB_PORT_NUM, CONFIG_MB_UART_TXD,
                            CONFIG_MB_UART_RXD, CONFIG_MB_UART_RTS,
                            UART_PIN_NO_CHANGE));  // Set UART pin numbers
    ESP_ERROR_CHECK(uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX)); // Set UART driver mode to Half Duplex
    ESP_LOGI(SLAVE_TAG, "Modbus slave stack initialized.");
    ESP_LOGI(SLAVE_TAG, "Start modbus test...");


    //-----------------------------------------SETUP SCALE-----------------------------------------
    int32_t cell1_data=0;
    int32_t cell2_data=0;
    int32_t cell3_data=0;
    int32_t cell4_data=0;
    int32_t pesoTot_data=0;
    int32_t pesoDX=0;
    int32_t pesoSX=0;
    int32_t pesoCalib = 0;
    int32_t ms = 0;
    int32_t imbalanceLimit = 0;
    int32_t act_imbalanceLimit = 0;
    int32_t maximumLimit = 0;
    int32_t act_maximumLimit = 0;
    uint16_t storeID = 0;
    uint16_t workplaceID = 0;
    bool prevTare=0;
    bool prevCalib=0;
    bool checkCells=0;
    char stato='A';
    int wei = 0;
    int max = 0;
    int reg1 = 0;
    int reg2 = 0;
    int imab = 0;
    int cali = 0;
    time_t tStart;
    //time_t tStart_check;
    holding_reg_params.diagnostic=0;
    holding_reg_params.connection_dummy=1;
    holding_reg_params.version=VERSIONE;
    //ADC1, ChanA (gain 64)
    HX711* cond1=new HX711();
    cond1->init(ADC1_DATA, ADC1_CLOCK);
    cond1->set_gain(eGAIN_64); 
    cond1->set_algo(new Combination(new Mediana(buffer), new MediaMobile(5)));
    //ADC1, ChanB (gain 32)
    HX711* cond2=new HX711();
    cond2->init(ADC1_DATA, ADC1_CLOCK);
    cond2->set_gain(eGAIN_32); 
    cond2->set_algo(new Combination(new Mediana(buffer), new MediaMobile(5)));
    //ADC2, ChanA (gain 64)
    HX711* cond3=new HX711();
    cond3->init(ADC2_DATA, ADC2_CLOCK);
    cond3->set_gain(eGAIN_64); 
    cond3->set_algo(new Combination(new Mediana(buffer), new MediaMobile(5)));
    //ADC2, ChanB (gain 32)
    HX711* cond4=new HX711();
    cond4->init(ADC2_DATA, ADC2_CLOCK);
    cond4->set_gain(eGAIN_32); 
    cond4->set_algo(new Combination(new Mediana(buffer), new MediaMobile(5)));
    //CONDIZIONATORE DI SEGNALE
    COND* bilancia=new COND();
    HX711** conds = (HX711**) malloc(sizeof(HX711*)*4);
    conds[0]=cond1; //Bilancia basso destra
    conds[1]=cond2; //Bilancia alto destra
    conds[2]=cond3; //Bilancia alto sinistra
    conds[3]=cond4; //Bilancia basso sinistra
    bilancia->init(conds, 4); 
    //Defining a filtering algorithm
    ALGO* algo = new Dummy();
    bilancia->set_algo(algo);
    //Defining a memory saving system
    SPIFFS* mem = new SPIFFS();
    if(mem->systemOK()) {
        bilancia->set_mem(mem);
        imbalanceLimit = mem->retrieveImbalanceLimit();
        maximumLimit = mem->retrieveMaximumLimit();
        printf("max: %d, imba: %d", maximumLimit, imbalanceLimit);
        storeID = mem->retrieveStoreID();
        workplaceID = mem->retrieveWorkplaceID();
        portENTER_CRITICAL(&param_lock);
            holding_reg_params.imbalance_limitLS = uint16_t(imbalanceLimit & 0xFFFF);
            holding_reg_params.imbalance_limitMS = uint16_t((imbalanceLimit >> 16) & 0xFFFF);
            holding_reg_params.maximum_limitLS = uint16_t(maximumLimit & 0xFFFF);
            holding_reg_params.maximum_limitMS = uint16_t((maximumLimit >> 16) & 0xFFFF);
            holding_reg_params.store_ID = storeID;
            holding_reg_params.workplace_ID = workplaceID;
        portEXIT_CRITICAL(&param_lock);

    }

    int o=0;
    while(true) {
        //LETTURA CELLE DI CARICO
        pesoTot_data = bilancia->get_units(1);
        o=o+1;
        cell1_data = bilancia->get_last_units(0);
        cell2_data = bilancia->get_last_units(1);
        cell3_data = bilancia->get_last_units(2);
        cell4_data = bilancia->get_last_units(3);
        pesoDX = cell1_data + cell2_data;
        pesoSX = cell3_data + cell4_data;
        //AGGIORNAMENTO REGISTRI MODBUS
        portENTER_CRITICAL(&param_lock);
            //Presence status
            coil_reg_params.coil_PresenceStatus = !gpio_get_level(avvio_lettura);   //*ingresso in logica inversa
        portEXIT_CRITICAL(&param_lock);
        
        ms =  holding_reg_params.imbalance_limitMS * 65536;
        act_imbalanceLimit = ms + holding_reg_params.imbalance_limitLS;
        if(imbalanceLimit != act_imbalanceLimit) {
                printf("chamge imb");
                ms =  holding_reg_params.imbalance_limitMS * 65536;
                imbalanceLimit = ms + holding_reg_params.imbalance_limitLS;
                mem->saveImbalanceLimit(imbalanceLimit);
        }
        ms = holding_reg_params.maximum_limitMS * 65536;
        act_maximumLimit = ms + holding_reg_params.maximum_limitLS;
        if(maximumLimit != act_maximumLimit) {
                printf("Act_MaximumLimit: %d, MaximumLimit: %d\n", act_maximumLimit, maximumLimit);
                ms = holding_reg_params.maximum_limitMS * 65536;
                maximumLimit = ms + holding_reg_params.maximum_limitLS;
                mem->saveMaximumLimit(maximumLimit);
        }
        if(storeID != holding_reg_params.store_ID) {
                storeID = holding_reg_params.store_ID;
                mem->saveStoreID(storeID);
        }
        if(workplaceID != holding_reg_params.workplace_ID) {
                workplaceID = holding_reg_params.workplace_ID;
                mem->saveWorkplaceID(workplaceID);
        }
        //DEFINIZIONE PROTOCOLLO DI CALIBRAZIONE
        if(coil_reg_params.coil_TareCommand==1) { //Intercettazione comando di tara
            if(prevTare==0) { //Rising edge
                coil_reg_params.coil_LastCommandSuccess=0;
                bilancia->tare(times*2);
                printf("Tare Command requested!\n");
                prevTare=1;
                coil_reg_params.coil_LastCommandSuccess=1;
            }
        }
        else prevTare=0;
        if(coil_reg_params.coil_CalibCommand==1) { //Intercettazione comando di calibrazione
            if(prevCalib==0) { //Rising edge
                coil_reg_params.coil_LastCommandSuccess=0;
                ms = holding_reg_params.holding_pesoCalibMS * 65536;
                pesoCalib =  ms + holding_reg_params.holding_pesoCalibLS;
                cali = int(pesoCalib);
                printf("cali: %d", cali);
                bilancia->calib(pesoCalib, times*2);
                printf("Calib Command requested!: %f\n", bilancia->get_calfact());
                prevCalib=1;
                coil_reg_params.coil_LastCommandSuccess=1;
            }
        }
        else prevCalib=0;
        //AUTOMA DI FUNZIONAMENTO BILANCIA
        if(stato=='A') {    //Stand-by: attesa vassoio
            if(coil_reg_params.coil_PresenceStatus==1) {    //Fronte di salita: presenza vassoio
                printf("Presenza vassoio ricevuta\n");
                coil_reg_params.coil_ReadingStatus=0;
                gpio_set_level(esito_lettura, 0);
                tStart=time(NULL);
                o=0;
                stato='B';
            }
        }
        else if(stato=='B') {    //Attesa misura vassoio
            if(difftime(time(NULL), tStart)>wait_time) {    //Tempo di attesa terminato
                printf("Tempo di attesa passato :\n");
                printf("Numero misure fatte: %d\n", o);
                int32_t diff = pesoDX - pesoSX;
                uint32_t current_imbalance = (pesoDX > pesoSX) ? (diff) : (-1*diff);    //absolute value
                ms = holding_reg_params.imbalance_limitMS*65536;
                uint32_t imbalance_limit =  ms + holding_reg_params.imbalance_limitLS;
                imab = (int)imbalance_limit;
                printf("imb: %d", imab);
                portENTER_CRITICAL(&param_lock);
                    if(current_imbalance > imbalance_limit) coil_reg_params.coil_ImabalanceStatus=1;
                    else coil_reg_params.coil_ImabalanceStatus=0;
                portEXIT_CRITICAL(&param_lock);
                //Controllo maximum weight
                int32_t current_weight = pesoTot_data;
                ms = holding_reg_params.maximum_limitMS*65536;
                int32_t maximum_limit =  ms + holding_reg_params.maximum_limitLS;
                reg1 = (int)holding_reg_params.maximum_limitMS;
                reg2 = (int)holding_reg_params.maximum_limitLS;
                max = (int)maximum_limit;
                printf("%d + %d = %d", reg1, reg2, max);
                portENTER_CRITICAL(&param_lock);
                    if(current_weight > maximum_limit) coil_reg_params.coil_MaximumStatus=1;
                    else coil_reg_params.coil_MaximumStatus=0;
                portEXIT_CRITICAL(&param_lock);
                //Consenso a PLC
                if(coil_reg_params.coil_MaximumStatus || coil_reg_params.coil_ImabalanceStatus) {   //Non abilitato prelievo vassoio
                    gpio_set_level(esito_controllo_peso, 0);
                    coil_reg_params.coil_OutcomeStatus=0;
                    checkCells=1;   //Comando di controllo celle di carico
                    printf("->Negazione prelievo vassoio \n");
                } 
                else {  //Abilitato prelievo vassoio
                    gpio_set_level(esito_controllo_peso, 1);
                    coil_reg_params.coil_OutcomeStatus=1;
                    checkCells=1;   //Comando di controllo celle di carico
                    //tStart_check=time(NULL);
                    printf("->Abilitazione prelievo vassoio \n");
                }
                wei = (int)current_weight;
                max = (int)maximum_limit;
                printf("Valore i: %d, MAX: %d\n", wei, max);
                coil_reg_params.coil_ReadingStatus=1;
                gpio_set_level(esito_lettura, 1);
                //Aggiorno registri Modbus
                portENTER_CRITICAL(&param_lock);
                    holding_reg_params.holding_cell1LS=uint16_t(cell1_data);
                    holding_reg_params.holding_cell1MS=cell1_data>>16;
                    holding_reg_params.holding_cell2LS=uint16_t(cell2_data);
                    holding_reg_params.holding_cell2MS=cell2_data>>16;
                    holding_reg_params.holding_cell3LS=uint16_t(cell3_data);
                    holding_reg_params.holding_cell3MS=cell3_data>>16;
                    holding_reg_params.holding_cell4LS=uint16_t(cell4_data);
                    holding_reg_params.holding_cell4MS=cell4_data>>16;
                    holding_reg_params.holding_pesoTotLS=uint16_t(current_weight);
                    holding_reg_params.holding_pesoTotMS=current_weight>>16;    //Prima era pesoTotData
                    holding_reg_params.holding_pesoDXLS=uint16_t(pesoDX);
                    holding_reg_params.holding_pesoDXMS=pesoDX>>16;
                    holding_reg_params.holding_pesoSXLS=uint16_t(pesoSX);
                    holding_reg_params.holding_pesoSXMS=pesoSX>>16;
                    holding_reg_params.count=holding_reg_params.count+1;
                    //Diagnosic code
                    if(bilancia->check_adcs(0)) holding_reg_params.diagnostic |= 1UL << 0;
                    else holding_reg_params.diagnostic &= ~(1UL << 0);
                    if(bilancia->check_adcs(1)) holding_reg_params.diagnostic |= 1UL << 1;
                    else holding_reg_params.diagnostic &= ~(1UL << 1);
                    if(bilancia->check_adcs(2)) holding_reg_params.diagnostic |= 1UL << 2;
                    else holding_reg_params.diagnostic &= ~(1UL << 2);
                    if(bilancia->check_adcs(3)) holding_reg_params.diagnostic |= 1UL << 3;
                    else holding_reg_params.diagnostic &= ~(1UL << 3);
                    if(!mem->systemOK()) holding_reg_params.diagnostic |= 1UL << 4;
                    else holding_reg_params.diagnostic &= ~(1UL << 4);
                portEXIT_CRITICAL(&param_lock);
                stato='C';
            }
        }
        else if(stato=='C') {   //Attesa fronte di discesa
            if(coil_reg_params.coil_PresenceStatus==0) {    //Fronte di discesa
                printf("Presenza vassoio non piu ricevuta\n");
                tStart=time(NULL);
                coil_reg_params.coil_ReadingStatus=0;
                gpio_set_level(esito_lettura, 0);

                stato='A';
            }
        }

        if(checkCells==1  && coil_reg_params.coil_PresenceStatus==0) {  //Controllo correttezza celle di carico
            printf("Controllando stato celle\n");
            unsigned int result_cells = bilancia->check_loadCells(err_perc);
            if(result_cells>0) {
                printf("Errore celle\n");
                portENTER_CRITICAL(&param_lock);
                    coil_reg_params.coil_CellStatus = 1;
                    if(result_cells==1) holding_reg_params.diagnostic |= 1UL << 5;
                    else holding_reg_params.diagnostic &= ~(1UL << 5);
                    if(result_cells==2) holding_reg_params.diagnostic |= 1UL << 6;
                    else holding_reg_params.diagnostic &= ~(1UL << 6);
                    if(result_cells==3) holding_reg_params.diagnostic |= 1UL << 7;
                    else holding_reg_params.diagnostic &= ~(1UL << 7);
                    if(result_cells==4) holding_reg_params.diagnostic |= 1UL << 8;
                    else holding_reg_params.diagnostic &= ~(1UL << 8);
                portEXIT_CRITICAL(&param_lock);
            }
            else {
                //bilancia->tare(times);
                portENTER_CRITICAL(&param_lock);
                    coil_reg_params.coil_CellStatus = 0;
                portEXIT_CRITICAL(&param_lock);
            }
            printf("Controllando stato adcs\n");
            unsigned int result_adcs = bilancia->check_adcs();
            if(result_adcs>0) {
                printf("Errore adcs\n");
                portENTER_CRITICAL(&param_lock);
                    coil_reg_params.coil_AdcsStatus = 1;
                portEXIT_CRITICAL(&param_lock);
            }
            else {
                portENTER_CRITICAL(&param_lock);
                    coil_reg_params.coil_AdcsStatus = 0;
                portEXIT_CRITICAL(&param_lock);
            }
            printf("Tutto ok\n");
            checkCells=0;
        }
    }
    // Destroy of Modbus controller on alarm
    ESP_LOGI(SLAVE_TAG,"Modbus controller destroyed.");
    vTaskDelay(100);
    ESP_ERROR_CHECK(mbc_slave_destroy());
    //Deallocate data structures
    algo->free_mem();
    bilancia->free_mem();
    free(conds);
}