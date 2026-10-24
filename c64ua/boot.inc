; SPDX-License-Identifier: GPL-3.0-only
!cpu 6502
*=$0801
!word basic_end
!word 10
!byte $9e
!text "2064"
!byte 0
basic_end: !word 0
!fill $0810-*,0
