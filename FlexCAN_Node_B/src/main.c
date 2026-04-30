#include "sdk_project_config.h"
#include <stdint.h>
#include <stdbool.h>
//Commit 2
/* Define Pins/Ports/Interrupt etc. */
#define EVB

#ifdef EVB
    #define LED_PORT        PORTD
    #define GPIO_PORT       PTD
    #define PCC_INDEX       PCC_PORTD_INDEX
    #define LED0            15U
    #define LED1            16U
    #define LED2            0U

    #define BTN_GPIO        PTC
    #define BTN1_PIN        13U
    #define BTN2_PIN        12U
    #define BTN_PORT        PORTC
    #define BTN_PORT_IRQn   PORTC_IRQn
#else
	/*Reserved block for different board configurations if needed */
#endif

uint8_t blinkCount = 5;
volatile bool flag_button_event = false;
volatile bool flag_can_rx = false;

flexcan_msgbuff_t recvBuff;


/*#define MASTER*/
#define SLAVE

/* Define the RX and TX mailbox and message configuration for whether the node will be the master or slave board */
#if defined(MASTER)
    #define TX_MAILBOX  (1UL)
    #define TX_MSG_ID   (1UL)
    #define RX_MAILBOX  (0UL)
    #define RX_MSG_ID   (2UL)
#elif defined(SLAVE)
    #define TX_MAILBOX  (0UL)
    #define TX_MSG_ID   (2UL)
    #define RX_MAILBOX  (1UL)
    #define RX_MSG_ID   (1UL)
#endif

/* CAN Message payloads defined here */
typedef enum
{
    LED0_CHANGE_REQUESTED = 0x00U,
    LED1_CHANGE_REQUESTED = 0x01U
} can_commands_list;

volatile uint8_t ledRequested = (uint8_t)LED0_CHANGE_REQUESTED;

bool useEncryption = false;

/*
	Function Prototypes
*/
void SendCANData(uint32_t mailbox, uint32_t messageId, uint8_t * data, uint32_t len);
void buttonISR(void);
void BoardInit(void);
void GPIOInit(void);
void FlexCANInit(void);

void canRxCallback(uint8_t instance,
                   flexcan_event_type_t eventType,
                   uint32_t buffIdx,
                   flexcan_state_t *state);

/*
	Functions Section
*/

/*
	Button ISR interrupt handler section
 */
void buttonISR(void)
{
    uint32_t buttonsPressed = PINS_DRV_GetPortIntFlag(BTN_PORT) &
                              ((1 << BTN1_PIN) | (1 << BTN2_PIN));

    if (buttonsPressed != 0)
    {
        if (buttonsPressed & (1 << BTN1_PIN))
        {
        	ledRequested = LED0_CHANGE_REQUESTED;
            PINS_DRV_ClearPinIntFlagCmd(BTN_PORT, BTN1_PIN);
        }
        else if (buttonsPressed & (1 << BTN2_PIN))
        {
        	ledRequested = LED1_CHANGE_REQUESTED;
            PINS_DRV_ClearPinIntFlagCmd(BTN_PORT, BTN2_PIN);
        }

        flag_button_event = true;
    }
}

/*
 * @brief: Send data via CAN to the specified mailbox with the specified message id
 * @param mailbox   : Destination mailbox number
 * @param messageId : Message ID
 * @param data      : Pointer to the TX data
 * @param len       : Length of the TX data
 * @return          : None
 */
void SendCANData(uint32_t mailbox, uint32_t messageId, uint8_t * data, uint32_t len)
{
    /* Set information about the data to be sent
     *  - 1 byte in length
     *  - Standard message ID
     *  - Bit rate switch enabled to use a different bitrate for the data segment
     *  - Flexible data rate enabled
     *  - Use zeros for FD padding
     */
    flexcan_data_info_t dataInfo =
    {
            .data_length = len,
            .msg_id_type = FLEXCAN_MSG_ID_STD,
            .enable_brs  = true,
            .fd_enable   = true,
            .fd_padding  = 0U
    };

    /* Configure TX message buffer with index TX_MSG_ID and TX_MAILBOX*/
    FLEXCAN_DRV_ConfigTxMb(INST_FLEXCAN_CONFIG_1, mailbox, &dataInfo, messageId);

    /* Execute send non-blocking */
    FLEXCAN_DRV_Send(INST_FLEXCAN_CONFIG_1, mailbox, &dataInfo, messageId, data);
}

/*
 * @brief : Initialize clocks, pins and power modes
 */
void BoardInit(void)
{

	/* Configure clocks for PORT */
	CLOCK_DRV_Init(&clockMan1_InitConfig0);


    /* Initialize pins
     *  -   Init FlexCAN and GPIO pins
     *  -   See PinSettings component for more info
     */
    PINS_DRV_Init(NUM_OF_CONFIGURED_PINS0, g_pin_mux_InitConfigArr0);
}

/*
 * @brief Function which configures the LEDs and Buttons
 */
void GPIOInit(void)
{
    /* Output direction for LEDs */
    PINS_DRV_SetPinsDirection(GPIO_PORT, (1 << LED2) | (1 << LED1) | (1 << LED0));

    /* Set Output value LEDs */
    PINS_DRV_ClearPins(GPIO_PORT, 1 << LED1);
    PINS_DRV_SetPins(GPIO_PORT, 1 << LED2);

    /* Setup button pin */
    PINS_DRV_SetPinsDirection(BTN_GPIO, ~((1 << BTN1_PIN)|(1 << BTN2_PIN)));

    /* Setup button pins interrupt */
    PINS_DRV_SetPinIntSel(BTN_PORT, BTN1_PIN, PORT_INT_RISING_EDGE);
    PINS_DRV_SetPinIntSel(BTN_PORT, BTN2_PIN, PORT_INT_RISING_EDGE);

    /* Install buttons ISR */
    INT_SYS_InstallHandler(BTN_PORT_IRQn, &buttonISR, NULL);

    /* Enable buttons interrupt */
    INT_SYS_EnableIRQ(BTN_PORT_IRQn);
}

/*
 * @brief Initialize FlexCAN driver and configure the bit rate
 */
void FlexCANInit(void)
{
    /*
     * Initialize FlexCAN driver
     *  - 8 byte payload size
     *  - FD enabled
     *  - Bus clock as peripheral engine clock
     */
    FLEXCAN_DRV_Init(INST_FLEXCAN_CONFIG_1, &flexcanState0, &flexcanInitConfig0);
    FLEXCAN_DRV_InstallEventCallback(INST_FLEXCAN_CONFIG_1, canRxCallback, NULL);
}

void canRxCallback(uint8_t instance,
                   flexcan_event_type_t eventType,
                   uint32_t buffIdx,
                   flexcan_state_t *state)
{
    if ((eventType == FLEXCAN_EVENT_RX_COMPLETE) && (buffIdx == RX_MAILBOX))
    {
        flag_can_rx = true;
    }
}

volatile int exit_code = 0;

int main(void)
{


    /* Call the setup inits */
    BoardInit();
    GPIOInit();
    FlexCANInit();

    /* Set information about the data to be received
     *  - 1 byte in length
     *  - Standard message ID
     *  - Bit rate switch enabled to use a different bitrate for the data segment
     *  - Flexible data rate enabled
     *  - Use zeros for FD padding
     */
    flexcan_data_info_t dataInfo =
    {
            .data_length = 1U,
            .msg_id_type = FLEXCAN_MSG_ID_STD,
            .enable_brs  = true,
            .fd_enable   = true,
            .fd_padding  = 0U
    };

    /* Configure RX message buffer with index RX_MSG_ID and RX_MAILBOX */
    FLEXCAN_DRV_ConfigRxMb(INST_FLEXCAN_CONFIG_1, RX_MAILBOX, &dataInfo, RX_MSG_ID);

    FLEXCAN_DRV_Receive(INST_FLEXCAN_CONFIG_1, RX_MAILBOX, &recvBuff);

    while(1)
    {
    	if (flag_button_event)
    	{
    	    flag_button_event = false;
    	    uint8_t txData = ledRequested;
    	    SendCANData(TX_MAILBOX, TX_MSG_ID, &txData, 1U);
    	}

    	if (flag_can_rx)
    	{
    	    flag_can_rx = false;

    	    // Process message
    	    if ((recvBuff.data[0] == LED0_CHANGE_REQUESTED) &&
    	        (recvBuff.msgId == RX_MSG_ID))
    	    {
    	        // trigger LED0 action (still blocking for now)
            	for (int i = 0; i < blinkCount; i++){
            		/* Toggle output value LED0 */
            		PINS_DRV_TogglePins(GPIO_PORT, (1 << LED0));
            		OSIF_TimeDelay(150); //Delay
            	}
    	    }
    	    else if ((recvBuff.data[0] == LED1_CHANGE_REQUESTED) &&
    	             (recvBuff.msgId == RX_MSG_ID))
    	    {
    	        // trigger LED1 action
            	for (int i = 0; i < blinkCount; i++){
                    /* Toggle output value LED1 */
                    PINS_DRV_TogglePins(GPIO_PORT, (1 << LED1));
                    OSIF_TimeDelay(150); //Delay
            	}
    	    }

    	    // RE-ARM receive
    	    FLEXCAN_DRV_Receive(INST_FLEXCAN_CONFIG_1, RX_MAILBOX, &recvBuff);
    	}
    }
  for(;;) {
    if(exit_code != 0) {
      break;
    }
  }
  return exit_code;
}



