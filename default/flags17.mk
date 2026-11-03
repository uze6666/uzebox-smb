## flags17.mk -- the compiler's flags each C object gets more (the Makefile includes it):
## the ones that make the linked ROM smaller, found an object at a time. Tuned to these
## sources: a change to an object may make its flags worth less.
##
## Written 2026-10-07 10:10 on commit bb0d540, avr-gcc 14.2.0.
## Both ROMs built with it by the Makefile, against FLAGS17=0:
##   the release ROM 65286 > 64722 bytes, the debug ROM 65460 > 64878;
##   SRAM 3566 > 3554, the stack 214 bytes.
##
## What each object gained, its flags alone on the linked ROM (bytes of flash; SRAM):
##   object              release    debug   SRAM
##   uzeboxCore               -2       -2      0
##   uzeboxVideoEngine       -32      -32      0
##   bb                      -16      -16      0
##   area                    -20      -20      0
##   player                  -62      -62      0
##   objects                 -60      -60      0
##   enemies                 -70      -70      0
##   bosses                  -66      -66    -12
##   collide                   none pays
##   bgcoll                  -54      -54      0
##   sprites                 -64      -64      0
##   gfx17                   -30      -44      0
##   text                     -2       -4      0
##   sound                   -72      -72      0
##   apu                       none pays
##   game                    -14      -14      0
##   main17                    0       -2      0

FLAGS17_uzeboxCore = -mno-call-prologues
FLAGS17_uzeboxVideoEngine = -fno-move-loop-invariants -fno-cse-follow-jumps -fno-split-wide-types -flive-range-shrinkage -fno-tree-forwprop -mstrict-X
FLAGS17_bb = -O2 -fno-gcse
FLAGS17_area = -fno-tree-sink -fno-tree-coalesce-vars -frename-registers -mstrict-X -fno-tree-loop-ivcanon
FLAGS17_player = -fno-ssa-phiopt -fno-tree-fre -fno-expensive-optimizations -fno-tree-loop-ivcanon -fno-tree-dce -fno-tree-forwprop
FLAGS17_objects = -mbranch-cost=3 -fno-tree-coalesce-vars -fno-ssa-phiopt -fsplit-wide-types -fno-tree-reassoc -frename-registers
FLAGS17_enemies = -mstrict-X -fno-tree-vrp -fno-move-loop-invariants -fno-store-merging -frename-registers -fno-tree-tail-merge -fno-if-conversion -fno-tree-dce -fno-tree-loop-im
FLAGS17_bosses = -fno-bit-tests -fno-tree-tail-merge -fno-tree-ter -fno-toplevel-reorder -fno-forward-propagate -fno-move-loop-invariants -fno-reorder-blocks -fno-tree-sink
FLAGS17_bgcoll = -fno-expensive-optimizations -fno-ssa-phiopt -fno-store-merging
FLAGS17_sprites = -fno-expensive-optimizations -fno-tree-tail-merge -fno-tree-loop-optimize -fno-ira-hoist-pressure -frename-registers
FLAGS17_gfx17 = -fno-tree-loop-optimize -fno-tree-forwprop -mstrict-X -fno-code-hoisting -fno-tree-tail-merge
FLAGS17_text = -fno-tree-loop-im -mbranch-cost=2
FLAGS17_sound = -fno-ipa-cp -fno-inline-functions-called-once -fno-ssa-phiopt --param=case-values-threshold=12 -fno-move-loop-invariants -fno-thread-jumps -fno-tree-sink -fno-forward-propagate
FLAGS17_game = -fno-inline-functions-called-once -fno-tree-dse -frename-registers -fno-ivopts
FLAGS17_main17 = -fno-tree-sink
