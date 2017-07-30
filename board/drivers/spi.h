// IRQs: DMA2_Stream2, DMA2_Stream3, EXTI4

#define SPI_BUF_SIZE 256
uint8_t spi_buf[SPI_BUF_SIZE];
int spi_buf_count = 0;
int spi_total_count = 0;

void spi_init() {
  //puts("SPI init\n");
  SPI1->CR1 = SPI_CR1_SPE;

  // enable SPI interrupts
  //SPI1->CR2 = SPI_CR2_RXNEIE | SPI_CR2_ERRIE | SPI_CR2_TXEIE;
  SPI1->CR2 = SPI_CR2_RXNEIE;

  NVIC_EnableIRQ(DMA2_Stream2_IRQn);
  NVIC_EnableIRQ(DMA2_Stream3_IRQn);
  //NVIC_EnableIRQ(SPI1_IRQn);

  // reset handshake back to pull up
  GPIOB->MODER &= ~(GPIO_MODER_MODER0);
  GPIOB->PUPDR |= GPIO_PUPDR_PUPDR0_0;

  // setup interrupt on falling edge of SPI enable (on PA4)
  SYSCFG->EXTICR[2] = SYSCFG_EXTICR2_EXTI4_PA;
  EXTI->IMR = (1 << 4);
  EXTI->FTSR = (1 << 4);
  NVIC_EnableIRQ(EXTI4_IRQn);
}

void spi_tx_dma(void *addr, int len) {
  // disable DMA
  SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
  DMA2_Stream3->CR &= ~DMA_SxCR_EN;

  // DMA2, stream 3, channel 3
  DMA2_Stream3->M0AR = (uint32_t)addr;
  DMA2_Stream3->NDTR = len;
  DMA2_Stream3->PAR = (uint32_t)&(SPI1->DR);

  // channel3, increment memory, memory -> periph, enable
  DMA2_Stream3->CR = DMA_SxCR_CHSEL_1 | DMA_SxCR_CHSEL_0 | DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_EN;
  DMA2_Stream3->CR |= DMA_SxCR_TCIE;

  SPI1->CR2 |= SPI_CR2_TXDMAEN;

  // signal data is ready by driving low
  // esp must be configured as input by this point
  GPIOB->MODER &= ~(GPIO_MODER_MODER0);
  GPIOB->MODER |= GPIO_MODER_MODER0_0;
  GPIOB->ODR &= ~(GPIO_ODR_ODR_0);
}

void spi_rx_dma(void *addr, int len) {
  // disable DMA
  SPI1->CR2 &= ~SPI_CR2_RXDMAEN;
  DMA2_Stream2->CR &= ~DMA_SxCR_EN;

  // drain the bus
  uint8_t dat = SPI1->DR;
  (void)dat;

  // DMA2, stream 2, channel 3
  DMA2_Stream2->M0AR = (uint32_t)addr;
  DMA2_Stream2->NDTR = len;
  DMA2_Stream2->PAR = (uint32_t)&(SPI1->DR);

  // channel3, increment memory, periph -> memory, enable
  DMA2_Stream2->CR = DMA_SxCR_CHSEL_1 | DMA_SxCR_CHSEL_0 | DMA_SxCR_MINC | DMA_SxCR_EN;
  DMA2_Stream2->CR |= DMA_SxCR_TCIE;

  SPI1->CR2 |= SPI_CR2_RXDMAEN;
}

// ***************************** SPI IRQs *****************************

// can't go on the stack cause it's DMAed
uint8_t spi_tx_buf[0x44];

// SPI RX
void DMA2_Stream2_IRQHandler(void) {
  // ack
  DMA2->LIFCR = DMA_LIFCR_CTCIF2;
  int *resp_len = (int*)spi_tx_buf;
  memset(spi_tx_buf, 0xaa, 0x44);
  *resp_len = spi_cb_rx(spi_buf, 0x13, spi_tx_buf+4);
  #ifdef DEBUG_SPI
    puts("SPI write: ");
    puth(*resp_len);
    puts("\n");
  #endif
  spi_tx_dma(spi_tx_buf, *resp_len + 4);
}

// SPI TX
void DMA2_Stream3_IRQHandler(void) {
  // ack
  DMA2->LIFCR = DMA_LIFCR_CTCIF3;

  #ifdef DEBUG_SPI
    puts("SPI handshake\n");
  #endif  

  // reset handshake back to pull up
  GPIOB->MODER &= ~(GPIO_MODER_MODER0);
  GPIOB->PUPDR |= GPIO_PUPDR_PUPDR0_0;
}

void EXTI4_IRQHandler(void) {
  int pr = EXTI->PR;
  // SPI CS rising
  if (pr & (1 << 4)) {
    spi_total_count = 0;
    spi_rx_dma(spi_buf, 0x14);
    #ifdef DEBUG_SPI
      puts("exti4\n");
    #endif
  }
  EXTI->PR = pr;
}

