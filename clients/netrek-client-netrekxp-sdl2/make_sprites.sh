#!/bin/bash
# Build combined sprite sheet BMPs from individual ship direction BMPs
# Layout: 8 columns (ship types) x 32 rows (directions)
# Ship type order: SC DD CA BB AS SB GA AT (matches struct.h indices 0-7)

SRCDIR="../../submodules/netrek-client-netrekxp/resources/ships"
OUTDIR="bitmaps/shiplib"
mkdir -p "$OUTDIR"

TYPES=(sc dd ca bb as sb ga at)

# Map: directory_name -> suffix, cell_size
declare -A VARIANTS
VARIANTS[mono]="M 20"
VARIANTS[color]=" 20"
VARIANTS[color1]="1 20"
VARIANTS[grayscale]="G 20"
VARIANTS[tinted]="T 20"
VARIANTS[highres]="HR 80"

for RACE in fed rom kli ori ind; do
    for DIR_NAME in "${!VARIANTS[@]}"; do
        read -r SUFFIX CELL_SIZE <<< "${VARIANTS[$DIR_NAME]}"

        INDIR="$SRCDIR/$DIR_NAME/$RACE"
        OUTFILE="$OUTDIR/${RACE}ship${SUFFIX}.bmp"

        if [ ! -d "$INDIR" ]; then
            echo "Skipping $INDIR (not found)"
            continue
        fi

        echo -n "Building $OUTFILE (${CELL_SIZE}x${CELL_SIZE})... "

        # Build ordered list
        FILES=()
        MISSING=0
        for D in $(seq -w 1 32); do
            for TYPE in "${TYPES[@]}"; do
                F="$INDIR/${RACE}_${TYPE}${D}.bmp"
                if [ -f "$F" ]; then
                    FILES+=("$F")
                else
                    FILES+=("xc:black[${CELL_SIZE}x${CELL_SIZE}]")
                    MISSING=$((MISSING + 1))
                fi
            done
        done

        montage "${FILES[@]}" -tile 8x32 -geometry "${CELL_SIZE}x${CELL_SIZE}+0+0" \
            -background black "$OUTFILE" 2>/dev/null

        if [ -f "$OUTFILE" ]; then
            SIZE=$(identify -format "%wx%h" "$OUTFILE" 2>/dev/null)
            echo "OK ($SIZE, $MISSING missing)"
        else
            echo "FAILED"
        fi
    done
done

echo "Done. Files:"
ls -la "$OUTDIR/"
