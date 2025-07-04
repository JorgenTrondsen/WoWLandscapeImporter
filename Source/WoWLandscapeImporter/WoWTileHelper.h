#pragma once

#include "CoreMinimal.h"

namespace WoWTileHelper
{
    template <typename T>
    TArray<T> CropTile(const TArray<T> &ExpandedData, const uint16 ExpandedWidth, const uint16 CroppedWidth)
    {
        TArray<T> OutData;
        OutData.SetNumUninitialized(CroppedWidth * CroppedWidth);

        for (uint16 y = 0; y < CroppedWidth; ++y)
        {
            // Copy the first CroppedWidth pixels from each row of the input tile
            FMemory::Memcpy(&OutData[y * CroppedWidth], &ExpandedData[y * (ExpandedWidth)], CroppedWidth * sizeof(T));
        }

        return OutData;
    }

    template <typename T>
    TArray<T> ExpandTile(const TArray<T> &BaseData, const TArray<T> &TopRowData, const TArray<T> &LeftColumnData, const TArray<T> &TopLeftCornerData, const uint16 TileSize, const uint16 NumColumns, const uint16 NumRows)
    {
        const uint16 NewWidth = TileSize + NumColumns;
        const uint16 NewHeight = TileSize + NumRows;
        TArray<T> OutData;
        OutData.SetNumUninitialized(NewWidth * NewHeight);

        // Set the top `NumRows` rows, which consist of the corner and the top edge data
        for (uint16 y = 0; y < NumRows; ++y)
        {
            // Copy the corner data for this row (NumColumns pixels)
            if (TopLeftCornerData.Num() > 0)
                FMemory::Memcpy(&OutData[y * NewWidth], &TopLeftCornerData[y * NumColumns], NumColumns * sizeof(T));

            else // Fill with baseData value if no corner data available
                for (uint16 x = 0; x < NumColumns; ++x)
                {
                    OutData[y * NewWidth + x] = BaseData[0];
                }

            // Copy the top edge data for this row
            if (TopRowData.Num() > 0)
                FMemory::Memcpy(&OutData[y * NewWidth + NumColumns], &TopRowData[y * TileSize], TileSize * sizeof(T));

            else // Fill with BaseData[0] value if no top row data available
                for (uint16 x = 0; x < TileSize; ++x)
                {
                    OutData[y * NewWidth + NumColumns + x] = BaseData[0];
                }
        }

        // Set the remaining `TileSize` rows, which consist of the left edge and the base data
        for (uint16 y = 0; y < TileSize; ++y)
        {
            const uint16 DestY = y + NumRows;

            // Copy the left edge data for this row (NumColumns pixels)
            if (LeftColumnData.Num() > 0)
                FMemory::Memcpy(&OutData[DestY * NewWidth], &LeftColumnData[y * NumColumns], NumColumns * sizeof(T));

            else // Fill with BaseData[0] value if no left column data available
                for (uint16 x = 0; x < NumColumns; ++x)
                {
                    OutData[DestY * NewWidth + x] = BaseData[0];
                }

            // Copy the base data for this row (TileSize pixels)
            FMemory::Memcpy(&OutData[DestY * NewWidth + NumColumns], &BaseData[y * TileSize], TileSize * sizeof(T));
        }

        return OutData;
    }

    template <typename T>
    TArray<T> GetColumns(const TArray<T> &BaseData, const uint16 TileSize, const uint16 NumColumns)
    {
        TArray<T> OutData;
        OutData.SetNumUninitialized(TileSize * NumColumns);

        const uint16 StartColumn = (TileSize - 1) - NumColumns; // Skip the edge pixel (TileSize - 1)

        for (uint16 y = 0; y < TileSize; ++y)
        {
            // Copy the `NumColumns` pixels from the right side of the source row, excluding the edge
            FMemory::Memcpy(&OutData[y * NumColumns], &BaseData[y * TileSize + StartColumn], NumColumns * sizeof(T));
        }

        return OutData;
    }

    template <typename T>
    TArray<T> GetRows(const TArray<T> &BaseData, const uint16 TileSize, const uint16 NumRows)
    {
        TArray<T> OutData;
        OutData.SetNumUninitialized(TileSize * NumRows);

        const uint16 StartRow = (TileSize - 1) - NumRows; // Skip the edge pixel (TileSize - 1)

        for (uint16 y = 0; y < NumRows; ++y)
        {
            // Copy the `TileSize` pixels from the bottom of the source row, excluding the edge
            FMemory::Memcpy(&OutData[y * TileSize], &BaseData[(StartRow + y) * TileSize], TileSize * sizeof(T));
        }

        return OutData;
    }

    template <typename T>
    TArray<T> GetCorner(const TArray<T> &BaseData, const uint16 TileSize, const uint16 CornerSizeX, const uint16 CornerSizeY)
    {
        TArray<T> OutData;
        OutData.SetNumUninitialized(CornerSizeX * CornerSizeY);

        const uint16 CornerStartX = (TileSize - 1) - CornerSizeX; // Skip the edge pixel (TileSize - 1)
        const uint16 CornerStartY = (TileSize - 1) - CornerSizeY;

        for (uint16 y = 0; y < CornerSizeY; ++y)
        {
            // Copy the row of corner data
            FMemory::Memcpy(&OutData[y * CornerSizeX], &BaseData[(CornerStartY + y) * TileSize + CornerStartX], CornerSizeX * sizeof(T));
        }

        return OutData;
    }
}