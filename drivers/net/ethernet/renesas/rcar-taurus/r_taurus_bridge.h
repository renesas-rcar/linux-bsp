
#ifndef R_TAURUS_BRIDGE_H
#define R_TAURUS_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* This is file defines the basic command protocol between TAURUS and its guest.

   Protocol extensions for concrete peripherals are defines in sub folders of
   this directory. Basically such extensions would be a description of concrete
   IOCTL functionality. If the peripheral does not need such extensions, the
   basic protocoll should be sufficient.

   CAUTION:
   A guest mighe have a complete different architecture, but the protocol needs
   to be interpreted by host & guest in the same way (e.g. 64b vs 32b pointer &
   endianess. Therefore only explicit types and no pointers shall be used in this
   file. Endianess of the guest is assumed to be the same as for TAURUS itself.
   Padding of structures is basically also assumed to be the same for TAURUS and
   a guest. If there is any confluct, the guest will have to adapt to the
   protocol interpretation of TAURUS.
 */

/*******************************************************************************
  Section: Includes
*/

#ifndef __KERNEL__
#include <stdint.h>
#endif

/*******************************************************************************
  Section: Global Defines
*/

/* TAURUS command identifier */
#define R_TAURUS_CMD_NOP           0
#define R_TAURUS_CMD_OPEN          1
#define R_TAURUS_CMD_CLOSE         2
#define R_TAURUS_CMD_READ          3
#define R_TAURUS_CMD_WRITE         4
#define R_TAURUS_CMD_IOCTL         5
#define R_TAURUS_CMD_STATUS        6
#define R_TAURUS_CMD_EXIT          7

/* TAURUS command result values */
#define R_TAURUS_RES_ACK           0
#define R_TAURUS_RES_NACK          1
#define R_TAURUS_RES_COMPLETE      2
#define R_TAURUS_RES_ERROR         3

/* TAURUS signal identifier */
#define R_TAURUS_SIG_IRQ           0x10     /* Peripheral interrupt has occured */
#define R_TAURUS_SIG_ERROR         0x20     /* TAURUS detected an error */
#define R_TAURUS_SIG_FATAL_ERROR   0x30     /* TAURUS detected a fatal problem and does not work reliable */
#define R_TAURUS_SIG_REBOOTING     0x40     /* TAURUS will reboot */
#define R_TAURUS_SIG_REBOOT        0x50     /* TAURUS asks the guest to reboot */
#define R_TAURUS_SIG_RESET         0x60     /* TAURUS will reset the entire system including the guest */

/*******************************************************************************
  Section: Global Types
*/

/*******************************************************************************
  Type: R_TAURUS_CmdMsg_t

  TAURUS command message.

  Members:
  Id            - Transaction Id
  Per           - Identifier for the peripheral
  Channel       - Channel of the peripheral
  Cmd           - Command (Open, Read, Write, Close, IoCtl)
  Par1          - Auxiliary parameter, typically buffer
  Par2          - Auxiliary parameter, typically size
  Par3          - Auxiliary parameter
*/

typedef struct {
    uint32_t          Id;
    uint32_t          Per;
    uint32_t          Channel;
    uint32_t          Cmd;
    uint64_t          Par1;
    uint64_t          Par2;
    uint64_t          Par3;
} R_TAURUS_CmdMsg_t;

/*******************************************************************************
  Type: R_TAURUS_Result_t

  TAURUS command message.

  Members:
  Id            - Transaction Id
  Per           - Identifier for the peripheral
  Result        - Result (ACK, NAK, COMP, ERR)
  Aux           - Auxiliary result parameter (e.g. written data lentgh)
*/

typedef struct {
    uint32_t        Id;
    uint32_t        Per;
    uint32_t        Channel;
    uint32_t        Result;
    uint64_t        Aux;
} R_TAURUS_ResultMsg_t;


/*******************************************************************************
  Type: R_TAURUS_SignalId_t

  Identifier of signal sent to the guest.

  TAURUS can trigger an interrupt for the guest. This identifier specifies the
  reason for the interrupt. Usually this shall be used to inform the guest
  about peripheral interrupts, so that the guest can check all virtual drivers,
  but it can also signal TAURUS conditions.

  For details see: R_TAURUS_SIG_XXX definitions
*/

typedef uint32_t R_TAURUS_SignalId_t;

#ifdef __cplusplus
}
#endif

#endif /* R_TAURUS_BRIDGE_H */
