#pragma once

#include "CoreMinimal.h"

namespace HeightmapHelper
{
    /**
     * Crops a heightmap by taking a top-left sub-region of minimum size 257x257.
     * This is designed to crop a heightmap to 255x255 by removing the right-most and bottom-most rows/columns
     * @param InData The input heightmap data typically after expansion (e.g., 267x259)
     * @param NumColumns The number of columns the heightmap was expanded by
     * @return The cropped 255x255 heightmap data
     */
    TArray<uint16> CropHeightmap(const TArray<uint16> &InData, uint8 NumColumns);

    /**
     * Expands a heightmap by adding a new row on top and a new column on the left.
     * This can be used to create a heightmap from a 257x257 base by adding edges from neighbor tiles
     * @param BaseData The base heightmap data (e.g., 257x257)
     * @param TopRowData The data for the new top row
     * @param LeftColumnData The data for the new left column
     * @param TopLeftCornerData The data for the new top left corner(e.g., 2x2 corner if NumColumns and NumRows are both 2)
     * @param NumColumns The number of new columns to add on the left
     * @param NumRows The number of new rows to add on the top
     * @return The expanded heightmap data
     */
    TArray<uint16> ExpandHeightmap(const TArray<uint16> &BaseData, const TArray<uint16> &TopRowData, const TArray<uint16> &LeftColumnData, const TArray<uint16> &TopLeftCornerData, uint8 NumColumns, uint8 NumRows);

    /**
     * Extracts columns from a heightmap, starting from right (excluding the edge pixel)
     * @param BaseData The source heightmap data (257x257)
     * @param NumColumns The number of columns to extract from the heightmap
     * @return The columns data (256 x NumColumns, excluding the shared edge)
     */
    TArray<uint16> GetColumns(const TArray<uint16> &BaseData, uint8 NumColumns);

    /**
     * Extracts rows from a heightmap, starting from bottom (excluding the edge pixel)
     * @param BaseData The source heightmap data (257x257)
     * @param NumRows The number of rows to extract from the heightmap
     * @return The rows data (NumRows x 256, excluding the shared edge)
     */
    TArray<uint16> GetRows(const TArray<uint16> &BaseData, uint8 NumRows);

    /**
     * Extracts corner data from a heightmap, starting from bottom-right (excluding the edge pixels)
     * @param BaseData The source heightmap data (257x257)
     * @param CornerSizeX The width of the corner to extract
     * @param CornerSizeY The height of the corner to extract
     * @return The corner data (CornerSizeX x CornerSizeY, excluding the shared edges)
     */
    TArray<uint16> GetCorner(const TArray<uint16> &BaseData, uint8 CornerSizeX, uint8 CornerSizeY);
}