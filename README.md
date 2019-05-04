# psoc-test-dma
Sample code to test DMA to custom UDB DataPath component.

# Description
This is a sample code to record how to use DMA to custom UDB DataPath component on PSoC.
It's mostly for later reference.

BitBlit component is a simple UART component implemented in UDB DataPath.
In this sample, I have implemented 6 ways to tranfer in-memory buffer to the device, using either DMA or non-DMA method.

# How to use
Edit main.c and set DMA_METHOD macro to either of followings.

  * DMA_NONE
    * Repeating transfer using non-DMA, pure software-driven method
  * DMA_WITH_DRQ_TYPE1
    * Repeating transfer using DMA with never-ending self-looping transfer descriptor
  * DMA_WITH_DRQ_TYPE2
    * Repeating transfer using DMA with hardware-driven, level-triggered DMAC drq input
  * DMA_WITH_DRQ_TYPE3
    * One-shot transfer using DMA with hardware-driven, level-triggered DMAC drq input
  * DMA_WITH_ISR_NRQ
    * Repeating transfer using ISR-driven DMA, using DMAC nrq output with device polling
  * DMA_WITH_ISR_READY
    * Repeating transfer using ISR-driven DMA, using component's READY output as a level-trigger

Also, in TopLevel schematic, enable/disable DMAC drq input based on method chosen.
All DMA_WITH_DRQ_* methods requires DMAC drq input to be enabled and connected to READY pin.
All other methods requires DMAC drq input to be disabled (= either disabled or disconnected from READY pin).
