#include "HeightmapHelper.h"

namespace HeightmapHelper
{
    TArray<uint16> CropHeightmap(const TArray<uint16> &InData, uint8 NumColumns)
    {
        TArray<uint16> OutData;
        OutData.SetNumUninitialized(255 * 255);

        for (uint16 y = 0; y < 255; ++y)
        {
            // Copy the first 255 pixels from each row of the input heightmap
            FMemory::Memcpy(&OutData[y * 255], &InData[y * (257 + NumColumns)], 255 * sizeof(uint16));
        }

        return OutData;
    }

    TArray<uint16> ExpandHeightmap(const TArray<uint16> &BaseData, const TArray<uint16> &TopRowData, const TArray<uint16> &LeftColumnData, const TArray<uint16> &TopLeftCornerData, uint8 NumColumns, uint8 NumRows)
    {
        const uint16 NewWidth = 257 + NumColumns;
        const uint16 NewHeight = 257 + NumRows;
        TArray<uint16> OutData;
        OutData.SetNumUninitialized(NewWidth * NewHeight);

        // Set the top `NumRows` rows, which consist of the corner and the top edge data
        for (uint16 y = 0; y < NumRows; ++y)
        {
            // Copy the corner data for this row (NumColumns pixels)
            if (TopLeftCornerData.Num() > 0)
                FMemory::Memcpy(&OutData[y * NewWidth], &TopLeftCornerData[y * NumColumns], NumColumns * sizeof(uint16));

            else // Fill with baseData value if no corner data available
                for (uint8 x = 0; x < NumColumns; ++x)
                {
                    OutData[y * NewWidth + x] = BaseData[0];
                }

            // Copy the top edge data for this row (256 pixels from TopRowData, excluding shared edge)
            if (TopRowData.Num() > 0)
                FMemory::Memcpy(&OutData[y * NewWidth + NumColumns], &TopRowData[y * 257], 257 * sizeof(uint16));

            else // Fill with BaseData[0] value if no top row data available
                for (uint16 x = 0; x < 256; ++x)
                {
                    OutData[y * NewWidth + NumColumns + x] = BaseData[0];
                }
        }

        // Set the remaining `257` rows, which consist of the left edge and the base data
        for (uint16 y = 0; y < 257; ++y)
        {
            const uint16 DestY = y + NumRows;

            // Copy the left edge data for this row (NumColumns pixels)
            if (LeftColumnData.Num() > 0)
                FMemory::Memcpy(&OutData[DestY * NewWidth], &LeftColumnData[y * NumColumns], NumColumns * sizeof(uint16));

            else // Fill with BaseData[0] value if no left column data available
                for (uint8 x = 0; x < NumColumns; ++x)
                {
                    OutData[DestY * NewWidth + x] = BaseData[0];
                }

            // Copy the base data for this row (257 pixels)
            FMemory::Memcpy(&OutData[DestY * NewWidth + NumColumns], &BaseData[y * 257], 257 * sizeof(uint16));
        }

        return OutData;
    }

    TArray<uint16> GetColumns(const TArray<uint16> &BaseData, uint8 NumColumns)
    {
        TArray<uint16> OutData;
        OutData.SetNumUninitialized(257 * NumColumns);

        const uint16 StartColumn = 256 - NumColumns; // Skip the edge pixel (257th column)

        for (uint16 y = 0; y < 257; ++y)
        {
            // Copy the `NumColumns` pixels from the right side of the source row, excluding the edge
            FMemory::Memcpy(&OutData[y * NumColumns], &BaseData[y * 257 + StartColumn], NumColumns * sizeof(uint16));
        }

        return OutData;
    }

    TArray<uint16> GetRows(const TArray<uint16> &BaseData, uint8 NumRows)
    {
        TArray<uint16> OutData;
        OutData.SetNumUninitialized(257 * NumRows);

        const uint16 StartRow = 256 - NumRows; // Skip the edge pixel (257th row)

        for (uint16 y = 0; y < NumRows; ++y)
        {
            // Copy the `257` pixels from the bottom of the source row, excluding the edge
            FMemory::Memcpy(&OutData[y * 257], &BaseData[(StartRow + y) * 257], 257 * sizeof(uint16));
        }

        return OutData;
    }

    TArray<uint16> GetCorner(const TArray<uint16> &BaseData, uint8 CornerSizeX, uint8 CornerSizeY)
    {
        TArray<uint16> OutData;
        OutData.SetNumUninitialized(CornerSizeX * CornerSizeY);

        const uint16 CornerStartX = 256 - CornerSizeX; // Skip the edge pixel (257th column)
        const uint16 CornerStartY = 256 - CornerSizeY; // Skip the edge pixel (257th row)

        for (uint16 y = 0; y < CornerSizeY; ++y)
        {
            // Copy the row of corner data
            FMemory::Memcpy(&OutData[y * CornerSizeX], &BaseData[(CornerStartY + y) * 257 + CornerStartX], CornerSizeX * sizeof(uint16));
        }

        return OutData;
    }
}