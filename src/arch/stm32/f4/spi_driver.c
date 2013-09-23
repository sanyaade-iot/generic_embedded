/*
 * spi_driver.c
 * Handles low level parts of spi bus
 *
 *  Created on: Aug 15, 2012
 *      Author: petera
 */

#include "spi_driver.h"
#include "spi_dev.h"
#include "miniutils.h"
#ifdef CONFIG_SPI

/*
 * Regarding TX:
 * To avoid busy-waiting on TX flag after DMA TX finish interrupt, we
 * always also receive the same amount of data. This will give us a convenient
 * DMA RX finish interrupt when all data is shifted out (as same amount
 * has been shifted in). The received data is put on the same address as
 * transmitted data, but transmitted data is put into shift register
 * before received data is put on the same address. Some energy is
 * wasted also but for the sake of simplicity...
 */

spi_bus __spi_bus_vec[SPI_MAX_ID];

// Finalizes hw blocks of a spi operation
static void SPI_finalize(spi_bus *s) {
#ifndef CONFIG_SPI_POLL
  DMA_Cmd(s->dma_tx_stream, DISABLE);
  DMA_Cmd(s->dma_rx_stream, DISABLE);
#endif
  SPI_Cmd(s->hw, DISABLE);
}

// IRQ: Finishes off an rx/tx operation, copies data and callbacks
static void SPI_finish(spi_bus *s, s32_t err) {
  if (s->busy) { // check should not be needed, but..
    if (s->rx_buf) {
      memcpy(s->rx_buf, s->buf, s->rx_len);
    }
    SPI_finalize(s);
    s->busy = FALSE;
    if (s->spi_bus_callback) {
      s->spi_bus_callback(s, err);
    }
  }
}

/* Initiates a spi rx/tx operation
   @param s               the spi bus to use
   @param tx_len          number of bytes to tx
   @param tx              tx buffer
                          if 0, things are sent from the bus struct's buffer (buf)
   @param rx_len          number of bytes to receive
   @param rx              where to actually put received bytes
                          if 0, things are received into the bus struct's buffer (buf)
 */
static void SPI_begin(spi_bus *s, u16_t tx_len, u8_t *tx, u16_t rx_len, u8_t *rx) {
#ifdef CONFIG_SPI_POLL
  u16_t tx_ix = 0;
  u16_t rx_ix = 0;
  if (rx == NULL) {
    rx_len = 0;
  }
  // clear rx buffer would we have txed something
  (void)SPI_I2S_ReceiveData(s->hw);

  while (tx_ix < tx_len || rx_ix < rx_len) {
    bool clock_data = TRUE;
    if (tx_ix < tx_len) {
      while (SPI_I2S_GetFlagStatus(s->hw, SPI_I2S_FLAG_TXE) == RESET);
      //print("T%02x ", s->buf[tx_ix]);
      SPI_I2S_SendData(s->hw, s->buf[tx_ix++]);
      clock_data = FALSE;
    }
    if (rx_ix < rx_len) {
      if (clock_data) {
        while (SPI_I2S_GetFlagStatus(s->hw, SPI_I2S_FLAG_TXE) == RESET);
        SPI_I2S_SendData(s->hw, 0xff);
      }
      while (SPI_I2S_GetFlagStatus(s->hw, SPI_I2S_FLAG_RXNE) == RESET);
      if (rx) {
        u8_t r = SPI_I2S_ReceiveData(s->hw);;
        rx[rx_ix++] = r;
        //print("R%02x ", r);
      } else {
        rx_ix++;
      }
    }
  }
  // wait until last tx char is entered into shift buffer
  while (SPI_I2S_GetFlagStatus(s->hw, SPI_I2S_FLAG_TXE) == RESET);
  // wait until last tx char is shifted out from buffer
  while (SPI_I2S_GetFlagStatus(s->hw, SPI_I2S_FLAG_RXNE) == SET) {
    (void)SPI_I2S_ReceiveData(s->hw);
  }
  DBG(D_SPI, D_DEBUG, " -- SPI tx:%04x rx:%04x\n", tx_ix, rx_ix);
  SPI_finish(s, SPI_OK);
#else

  // .. here be dragons...
  // seems to be a compiler thing - adding compile breaks
  DMA_Cmd(s->dma_rx_stream, DISABLE);
  DMA_Cmd(s->dma_tx_stream, DISABLE);

  __NOP();

  // set up tx channel
  if (tx_len == 0) {
    // only receiving, so send ff's to clock in data
    s->dummy = 0xff;
    s->dma_tx_stream->CR &= ~DMA_MemoryInc_Enable;
    s->dma_tx_stream->NDTR = rx_len;
    s->dma_tx_stream->M0AR = (u32_t)&s->dummy;
  } else {
    s->dma_tx_stream->CR |= DMA_MemoryInc_Enable;
    s->dma_tx_stream->NDTR = tx_len;
    s->dma_tx_stream->M0AR = (u32_t)(tx == 0 ? s->buf : tx);
  }

  __NOP();

  // set up rx channel
  if (rx_len == 0) {
    // only sending, but receive ignored data to get irq when all is rxed
    s->dma_rx_stream->CR &= ~DMA_MemoryInc_Enable;
    s->dma_rx_stream->NDTR = tx_len;
    s->dma_rx_stream->M0AR = (u32_t)&s->dummy;
  } else {
    s->dma_rx_stream->CR |= DMA_MemoryInc_Enable;
    s->dma_rx_stream->NDTR = rx_len;
    s->dma_rx_stream->M0AR = (u32_t)(rx == 0 ? s->buf : rx);
  }

  __NOP();

  DMA_Cmd(s->dma_rx_stream, ENABLE);
  DMA_Cmd(s->dma_tx_stream, ENABLE);
  SPI_Cmd(s->hw, ENABLE);

#endif // CONFIG_SPI_POLL
}

int SPI_close(spi_bus *s) {
  SPI_finalize(s);
  SPI_Cmd(s->hw, DISABLE);
  s->user_p = 0;
  s->user_arg = 0;
  if (s->busy) {
    s->busy = FALSE;
    return SPI_ERR_BUS_BUSY;
  }
  return SPI_OK;
}

static u16_t SPI_bitrate_to_proc_setting(u32_t bitrate, u32_t clock) {
  u32_t cand_bitrate = clock;
  // lowest prescaler is div by 2
  cand_bitrate >>= 1;
  u16_t prescaler = 1;
  while (prescaler < 8 && cand_bitrate > bitrate) {
    prescaler++;
    cand_bitrate >>= 1;
  }
  switch (prescaler) {
  case 1: return SPI_BaudRatePrescaler_2;
  case 2: return SPI_BaudRatePrescaler_4;
  case 3: return SPI_BaudRatePrescaler_8;
  case 4: return SPI_BaudRatePrescaler_16;
  case 5: return SPI_BaudRatePrescaler_32;
  case 6: return SPI_BaudRatePrescaler_64;
  case 7: return SPI_BaudRatePrescaler_128;
  default: return SPI_BaudRatePrescaler_256;
  }
}

int SPI_config(spi_bus *s, u16_t config) {
  SPI_InitTypeDef SPI_InitStructure;
  (void)SPI_close(s);

  SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
  SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
  SPI_InitStructure.SPI_CPOL =
      (config & SPIDEV_CONFIG_CPOL_MASK) == SPIDEV_CONFIG_CPOL_HI ? SPI_CPOL_High : SPI_CPOL_Low;
  SPI_InitStructure.SPI_CPHA =
      (config & SPIDEV_CONFIG_CPHA_MASK) == SPIDEV_CONFIG_CPHA_1E ? SPI_CPHA_1Edge : SPI_CPHA_2Edge;
  SPI_InitStructure.SPI_FirstBit =
      (config & SPIDEV_CONFIG_FBIT_MASK) == SPIDEV_CONFIG_FBIT_MSB ? SPI_FirstBit_MSB : SPI_FirstBit_LSB;
  SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
  u32_t clock = SystemCoreClock/2; // SPI clocked on APBm running core clock / 2
  switch (config & SPIDEV_CONFIG_SPEED_MASK) {
  case SPIDEV_CONFIG_SPEED_HIGHEST:
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    break;
  case SPIDEV_CONFIG_SPEED_18M:
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_bitrate_to_proc_setting(18000000, clock);
    break;
  case SPIDEV_CONFIG_SPEED_9M:
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_bitrate_to_proc_setting(9000000, clock);
    break;
  case SPIDEV_CONFIG_SPEED_4_5M:
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_bitrate_to_proc_setting(4500000, clock);
    break;
  case SPIDEV_CONFIG_SPEED_2_3M:
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_bitrate_to_proc_setting(2250000, clock);
    break;
  case SPIDEV_CONFIG_SPEED_1_1M:
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_bitrate_to_proc_setting(1125000, clock);
    break;
  case SPIDEV_CONFIG_SPEED_562_5K:
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_bitrate_to_proc_setting(562500, clock);
    break;
  case SPIDEV_CONFIG_SPEED_SLOWEST:
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
    break;
  }
  SPI_InitStructure.SPI_CRCPolynomial = 7;
  SPI_Init(s->hw, &SPI_InitStructure);

  return SPI_OK;
}

int SPI_tx(spi_bus *s, u8_t *tx, u16_t len) {
  if (s->busy) {
    return SPI_ERR_BUS_BUSY;
  }
  s->busy = TRUE;

  // read phony rx data into dummy byte
  s->rx_buf = 0; // no memcpy at DMA irq
  SPI_begin(s, len, tx, 0, 0);
  return SPI_OK;
}

int SPI_rx(spi_bus *s, u8_t *rx, u16_t len)  {
  if (s->busy) {
    return SPI_ERR_BUS_BUSY;
  }
  s->busy = TRUE;

  // read phony tx data from dummy byte
  s->rx_buf = 0; // no memcpy at DMA irq
  SPI_begin(s, 0, 0, len, rx);
  return SPI_OK;
}

int SPI_rxtx(spi_bus *s, u8_t *tx, u16_t tx_len, u8_t *rx, u16_t rx_len) {
  if (s->busy) {
    return SPI_ERR_BUS_BUSY;
  }
  s->busy = TRUE;
  u16_t maxlen = MAX(tx_len, rx_len);
  if (maxlen > s->max_buf_len) {
    s->busy = FALSE;
    return SPI_ERR_BUS_LEN_EXCEEDED;
  }

  // get tx data into temp buffer
  // rec rx data into temp buffer, memcpy after DMA is finished to actual
  // destination
  // this is due to tx_len might be bigger than rx len so DMA may not write
  // directly to rx buf
  memcpy(s->buf, tx, tx_len);
  s->rx_buf = rx; // memcpy at DMA finish irq
  s->rx_len = rx_len;
  SPI_begin(s, maxlen, 0, maxlen, 0);
  return SPI_OK;
}

int SPI_set_callback(spi_bus *spi, void (*spi_bus_callback)(spi_bus *s, s32_t res)) {
  if (spi->busy) {
    return SPI_ERR_BUS_BUSY;
  }
  spi->spi_bus_callback = spi_bus_callback;
  return SPI_OK;
}

void SPI_init() {
  // setup spi bus descriptor
  memset(__spi_bus_vec, 0, sizeof(__spi_bus_vec));

#if SPI_MAX_ID >= 1
  _SPI_BUS(0)->max_buf_len = SPI_BUFFER;
  _SPI_BUS(0)->hw = SPI2;
  _SPI_BUS(0)->dma_rx_irq = DMA_IT_TCIF3;
  _SPI_BUS(0)->dma_tx_irq = DMA_IT_TCIF4;
  _SPI_BUS(0)->dma_rx_err_tr_irq = DMA_IT_TEIF3;
  _SPI_BUS(0)->dma_tx_err_tr_irq = DMA_IT_TEIF4;
  _SPI_BUS(0)->dma_rx_err_dm_irq = DMA_IT_DMEIF3;
  _SPI_BUS(0)->dma_tx_err_dm_irq = DMA_IT_DMEIF4;
  _SPI_BUS(0)->dma_rx_stream = DMA1_Stream3;
  _SPI_BUS(0)->dma_tx_stream = DMA1_Stream4;
  _SPI_BUS(0)->dma_channel = DMA_Channel_0;
#endif
#if SPI_MAX_ID >= 2
  // TODO PETER

#endif
}

bool SPI_is_busy(spi_bus *spi) {
  return spi->busy;
}

void SPI_register(spi_bus *spi) {
  spi->attached_devices++;
}

void SPI_release(spi_bus *spi) {
  spi->attached_devices--;
  if (spi->attached_devices == 0) {
    SPI_close(spi);
  }
}

void SPI_irq(spi_bus *s) {
  bool report = FALSE;
  s32_t res = SPI_OK;

  if (DMA_GetITStatus(s->dma_rx_stream, s->dma_rx_err_dm_irq)) {
    // RX
    DMA_ClearITPendingBit(s->dma_rx_stream, s->dma_rx_err_dm_irq);
    report = TRUE;
    res = SPI_ERR_BUS_PHY;
  }
  if (DMA_GetITStatus(s->dma_tx_stream, s->dma_tx_err_dm_irq)) {
    // TX
    DMA_ClearITPendingBit(s->dma_tx_stream, s->dma_tx_err_dm_irq);
    report = TRUE;
    res = SPI_ERR_BUS_PHY;
  }
  if (DMA_GetITStatus(s->dma_rx_stream, s->dma_rx_err_tr_irq)) {
    // RX
    DMA_ClearITPendingBit(s->dma_rx_stream, s->dma_rx_err_tr_irq);
    report = TRUE;
    res = SPI_ERR_BUS_PHY;
  }
  if (DMA_GetITStatus(s->dma_tx_stream, s->dma_tx_err_tr_irq)) {
    // TX
    DMA_ClearITPendingBit(s->dma_tx_stream, s->dma_tx_err_tr_irq);
    report = TRUE;
    res = SPI_ERR_BUS_PHY;
  }

  if (DMA_GetITStatus(s->dma_rx_stream,s->dma_rx_irq)) {
    // RX
    DMA_ClearITPendingBit(s->dma_rx_stream,s->dma_rx_irq);
    // STM32F4 weirdness, TX halts if not doing this
    DMA_ClearITPendingBit(s->dma_tx_stream, s->dma_tx_irq);
    report = TRUE;
  }

  if (report) {
    SPI_finish(s, res);
  }
}

#endif
