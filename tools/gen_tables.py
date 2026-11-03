#!/usr/bin/env python3
"""gen_tables.py -- the engine's data tables, from your ROM: ../data/romtables.h.

    python gen_tables.py            writes ../data/romtables.h

The engine (area.c, enemies.c, sprites.c, ...) reads the 6502's own tables: the scenery, the
metatiles, the enemies' graphics, the physics, the attract mode's inputs... Each is a macro
here, ROM_<the C table's name>, made of pieces of your ROM: a table of the disassembly by its
label (tools/romdata.map), its first byte and its count, or ("PRG", address, count) for bytes the
disassembly has in the middle of code. The C files fill their arrays with the macros, so none
of the cartridge's bytes is in the source.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asmdata  # noqa: E402

OUT = os.path.join(HERE, "..", "data", "romtables.h")

TABLES = [
    ('BackSceneryData', [('BackSceneryData', 0, 144)]),
    ('BackSceneryMetatiles', [('BackSceneryMetatiles', 0, 36)]),
    ('ForeSceneryData', [('ForeSceneryData', 0, 39)]),
    ('TerrainMetatiles', [('TerrainMetatiles', 0, 4)]),
    ('TerrainRenderBits', [('TerrainRenderBits', 0, 32)]),
    ('BlockBuffLowBounds', [('BlockBuffLowBounds', 0, 4)]),
    ('Bitmasks', [('Bitmasks', 0, 8)]),
    ('CastleMetatiles', [('CastleMetatiles', 0, 55)]),
    ('SidePipeData', [('PRG', 0x989f, 12)]),
    ('VerticalPipeData', [('VerticalPipeData', 0, 8)]),
    ('RowColMetatiles', [('SolidBlockMetatiles', 0, 4), ('BrickMetatiles', 0, 5), ('CoinMetatileData', 0, 4)]),
    ('StaircaseHeightData', [('StaircaseHeightData', 0, 9)]),
    ('StaircaseRowData', [('StaircaseRowData', 0, 9)]),
    ('BrickQBlockMetatiles', [('BrickQBlockMetatiles', 0, 14)]),
    ('WarpZoneNumbers', [('WarpZoneNumbers', 0, 12)]),
    ('ClimbXPosAdder', [('PRG', 0xde25, 9)]),
    ('SolidMTileUpperExt', [('SolidMTileUpperExt', 0, 4)]),
    ('ClimbMTileUpperExt', [('ClimbMTileUpperExt', 0, 4)]),
    ('EnemyBGCStateData', [('PRG', 0xdfb9, 8)]),
    ('BoundBoxCtrlData', [('BoundBoxCtrlData', 0, 48)]),
    ('BlockBuffer_X_Adder', [('PRG', 0xe3b0, 56)]),
    ('BridgeCollapseData', [('BridgeCollapseData', 0, 15)]),
    ('PRandomRange', [('PRandomRange', 0, 4)]),
    ('FlameTimerData', [('FlameTimerData', 0, 8)]),
    ('StarFlagYPosAdder', [('StarFlagYPosAdder', 0, 4)]),
    ('StarFlagXPosAdder', [('StarFlagXPosAdder', 0, 4)]),
    ('StarFlagTileData', [('StarFlagTileData', 0, 4)]),
    ('KickedShellPtsData', [('KickedShellPtsData', 0, 3)]),
    ('SetBitsMask', [('SetBitsMask', 0, 7)]),
    ('LoopCmdWorldNumber', [('LoopCmdWorldNumber', 0, 11)]),
    ('LoopCmdPageNumber', [('LoopCmdPageNumber', 0, 11)]),
    ('LoopCmdYPosition', [('LoopCmdYPosition', 0, 11)]),
    ('AreaDataOfsLoopback', [('AreaDataOfsLoopback', 0, 11)]),
    ('PRDiffAdjustData', [('PRDiffAdjustData', 0, 12)]),
    ('FlyCCXPositionData', [('FlyCCXPositionData', 0, 16)]),
    ('FlyCCXSpeedData', [('FlyCCXSpeedData', 0, 12)]),
    ('FlyCCTimerData', [('FlyCCTimerData', 0, 4)]),
    ('FlameYPosData', [('FlameYPosData', 0, 4)]),
    ('FireworksXPosData', [('PRG', 0xc631, 12)]),
    ('Enemy17YPosData', [('Enemy17YPosData', 0, 8)]),
    ('FirebarPosLookupTbl', [('FirebarPosLookupTbl', 0, 99)]),
    ('FirebarMirrorData', [('FirebarMirrorData', 0, 4)]),
    ('FirebarTblOffsets', [('FirebarTblOffsets', 0, 12)]),
    ('PRandomSubtracter', [('PRG', 0xced5, 21)]),
    ('DemoActionData', [('DemoActionData', 0, 21)]),
    ('DemoTimingData', [('DemoTimingData', 0, 22)]),
    ('AreaPalette', [('AreaPalette', 0, 4)]),
    ('BGColorCtrl_Addr_', [('BGColorCtrl_Addr', 0, 4)]),
    ('FloateyNumTileData', [('FloateyNumTileData', 0, 24)]),
    ('ScoreUpdateData', [('ScoreUpdateData', 0, 12)]),
    ('Jumpspring_Y_PosData', [('Jumpspring_Y_PosData', 0, 4)]),
    ('PlayerStarting_X_Pos', [('PlayerStarting_X_Pos', 0, 4)]),
    ('PlayerStarting_Y_Pos', [('PlayerStarting_Y_Pos', 0, 9)]),
    ('HalfwayPageNybbles', [('HalfwayPageNybbles', 0, 16)]),
    ('Hidden1UpCoinAmts', [('Hidden1UpCoinAmts', 0, 8)]),
    ('ClimbAdderLow', [('ClimbAdderLow', 0, 4)]),
    ('JumpPhysData', [('PRG', 0xb424, 21)]),
    ('MaxRightXSpdData', [('MaxRightXSpdData', 0, 3)]),
    ('FrictionData', [('FrictionData', 0, 3)]),
    ('PlayerGfxTblOffsets', [('PlayerGfxTblOffsets', 0, 16)]),
    ('ChangeSizeOffsetAdder', [('ChangeSizeOffsetAdder', 0, 10)]),
    ('FlagpoleScoreNumTiles', [('FlagpoleScoreNumTiles', 0, 10)]),
    ('PowerUpGfxTable', [('PowerUpGfxTable', 0, 16)]),
    ('EnemyGraphicsTable', [('EnemyGraphicsTable', 0, 258)]),
    ('EnemyGfxTableOffsets', [('EnemyGfxTableOffsets', 0, 27)]),
    ('EnemyAttributeData', [('EnemyAttributeData', 0, 27)]),
    ('JumpspringFrameOffsets', [('JumpspringFrameOffsets', 0, 5)]),
    ('DefaultBlockObjTiles', [('DefaultBlockObjTiles', 0, 4)]),
    ('ExplosionTiles', [('ExplosionTiles', 0, 3)]),
    ('PlayerGraphicsTable', [('PlayerGraphicsTable', 0, 208)]),
    # the tails of tables whose first bytes are the 6502 code's own constants (kept in the C)
    ('OffscrXData', [('DefaultXOnscreenOfs', 0, 3), ('XOffscreenBitsData', 0, 16)]),
    ('OffscrYData', [('DefaultYOnscreenOfs', 0, 3), ('YOffscreenBitsData', 0, 9)]),
    ('DefaultSprOffsets', [('DefaultSprOffsets', 0, 15)]),
]


def main():
    prg, _ = asmdata.check_rom()
    L = ["/* romtables.h -- GENERATED by tools/gen_tables.py from your ROM; do not edit, never commit.",
         "** The 6502's tables the engine reads, ROM_<the C table's name>. */",
         "#pragma once", ""]
    for name, parts in TABLES:
        b = []
        for src, first, n in parts:
            if src == "PRG":
                piece = list(prg[first - 0x8000:first - 0x8000 + n])
            else:
                piece = asmdata.bytes_of(src)[first:first + n]
            if len(piece) != n:
                sys.exit("gen_tables: %s: %s has not %d bytes from %d" % (name, src, n, first))
            b += piece
        L.append("#define ROM_%s %s" % (name, ", ".join("0x%02x" % v for v in b)))
    with open(OUT, "w", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print("romtables.h: %d tables, %d bytes" % (len(TABLES), sum(n for _, p in TABLES for _, _, n in p)))


if __name__ == "__main__":
    main()
