#pragma once

#include "CoreMinimal.h"
#include "WoWLandscapeImporter.h"

namespace WoWTileHelper
{
    template <typename T>
    TArray<T> CropTile(const TArray<T> &ExpandedData, const uint16 ExpandedWidth, const uint16 CropStartX, const uint16 CropStartY, const uint16 CropEndX, const uint16 CropEndY)
    {
        const uint16 CroppedWidth = CropEndX - CropStartX;
        const uint16 CroppedHeight = CropEndY - CropStartY;

        TArray<T> OutData;
        OutData.SetNumUninitialized(CroppedWidth * CroppedHeight);

        for (uint16 y = 0; y < CroppedHeight; ++y)
        {
            // Copy the row of data from the expanded tile, starting at CropStartX and ending at CropEndX
            FMemory::Memcpy(&OutData[y * CroppedWidth], &ExpandedData[(CropStartY + y) * ExpandedWidth + CropStartX], CroppedWidth * sizeof(T));
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
    TArray<T> GetColumns(const TArray<T> &BaseData, const uint16 TileSize, const uint16 NumColumns, const bool bSkipEdge)
    {
        TArray<T> OutData;
        OutData.SetNumUninitialized(TileSize * NumColumns);

        const uint16 StartColumn = bSkipEdge ? (TileSize - 1) - NumColumns : TileSize - NumColumns; // Skip the edge pixel (TileSize - 1) if bSkipEdge is true

        for (uint16 y = 0; y < TileSize; ++y)
        {
            // Copy the `NumColumns` pixels from the right side of the source row, excluding the edge
            FMemory::Memcpy(&OutData[y * NumColumns], &BaseData[y * TileSize + StartColumn], NumColumns * sizeof(T));
        }

        return OutData;
    }

    template <typename T>
    TArray<T> GetRows(const TArray<T> &BaseData, const uint16 TileSize, const uint16 NumRows, const bool bSkipEdge)
    {
        TArray<T> OutData;
        OutData.SetNumUninitialized(TileSize * NumRows);

        const uint16 StartRow = bSkipEdge ? (TileSize - 1) - NumRows : TileSize - NumRows; // Skip the edge pixel (TileSize - 1) if bSkipEdge is true

        for (uint16 y = 0; y < NumRows; ++y)
        {
            // Copy the `TileSize` pixels from the bottom of the source row, excluding the edge
            FMemory::Memcpy(&OutData[y * TileSize], &BaseData[(StartRow + y) * TileSize], TileSize * sizeof(T));
        }

        return OutData;
    }

    template <typename T>
    TArray<T> GetCorner(const TArray<T> &BaseData, const uint16 TileSize, const uint16 CornerSizeX, const uint16 CornerSizeY, const bool bSkipEdge)
    {
        TArray<T> OutData;
        OutData.SetNumUninitialized(CornerSizeX * CornerSizeY);

        const uint16 CornerStartX = bSkipEdge ? (TileSize - 1) - CornerSizeX : TileSize - CornerSizeX; // Skip the edge pixel (TileSize - 1) if bSkipEdge is true
        const uint16 CornerStartY = bSkipEdge ? (TileSize - 1) - CornerSizeY : TileSize - CornerSizeY;

        for (uint16 y = 0; y < CornerSizeY; ++y)
        {
            // Copy the row of corner data
            FMemory::Memcpy(&OutData[y * CornerSizeX], &BaseData[(CornerStartY + y) * TileSize + CornerStartX], CornerSizeX * sizeof(T));
        }

        return OutData;
    }

    TArray<Chunk> ExtractTileChunks(const TArray<Chunk> &BaseChunks, const TArray<Chunk> &TopRowChunks, const TArray<Chunk> &LeftColumnChunks, const TArray<Chunk> &TopLeftCornerChunks, const uint16 NumColumns, const uint16 NumRows)
    {
        // The formula for slicing the BaseChunks array into the final output array
        const uint16 ColumnsToSlice = (8 + NumColumns) / 64;
        const uint16 RowsToSlice = (8 + NumRows) / 64;

        // The formula for merging the neighboring chunk arrays into the final output array
        const uint16 ColumnsToMerge = FMath::CeilToInt((float)NumColumns / 64.0f);
        const uint16 RowsToMerge = FMath::CeilToInt((float)NumRows / 64.0f);

        // Calculate the dimensions of the final output grid.
        const uint16 NewGridWidth = (16 - ColumnsToSlice) + ColumnsToMerge;
        const uint16 NewGridHeight = (16 - RowsToSlice) + RowsToMerge;

        TArray<Chunk> OutArray;
        OutArray.SetNum(NewGridWidth * NewGridHeight);

        // Iterate through every (Row, Col) of the destination grid.
        for (uint16 Row = 0; Row < NewGridHeight; ++Row)
        {
            for (uint16 Col = 0; Col < NewGridWidth; ++Col)
            {
                const uint16 DestIndex = Row * NewGridWidth + Col;

                // Determine which source array to pull from based on the (Row, Col).
                if (Row < RowsToMerge && Col < ColumnsToMerge)
                {
                    // TOP-LEFT CORNER Section: Pull from the bottom-right of TopLeftCornerChunks.
                    if (TopLeftCornerChunks.Num() > 0)
                    {
                        const uint16 SourceRow = (16 - RowsToMerge) + Row;
                        const uint16 SourceCol = (16 - ColumnsToMerge) + Col;
                        const uint16 SourceIndex = SourceRow * 16 + SourceCol;
                        OutArray[DestIndex] = TopLeftCornerChunks[SourceIndex];
                    }
                    else
                    {
                        OutArray[DestIndex] = Chunk();
                    }
                }
                else if (Row < RowsToMerge)
                {
                    // TOP ROW Section: Pull from the bottom rows of TopRowChunks.
                    if (TopRowChunks.Num() > 0)
                    {
                        const uint16 SourceRow = (16 - RowsToMerge) + Row;
                        const uint16 SourceCol = Col - ColumnsToMerge; // Adjust column to be relative to this section
                        const uint16 SourceIndex = SourceRow * 16 + SourceCol;
                        OutArray[DestIndex] = TopRowChunks[SourceIndex];
                    }
                    else
                    {
                        OutArray[DestIndex] = Chunk();
                    }
                }
                else if (Col < ColumnsToMerge)
                {
                    // LEFT COLUMN Section: Pull from the right columns of LeftColumnChunks.
                    if (LeftColumnChunks.Num() > 0)
                    {
                        const uint16 SourceRow = Row - RowsToMerge; // Adjust row to be relative to this section
                        const uint16 SourceCol = (16 - ColumnsToMerge) + Col;
                        const uint16 SourceIndex = SourceRow * 16 + SourceCol;
                        OutArray[DestIndex] = LeftColumnChunks[SourceIndex];
                    }
                    else
                    {
                        OutArray[DestIndex] = Chunk();
                    }
                }
                else
                {
                    // BASE Section: Pull from the top-left of BaseChunks.
                    if (BaseChunks.Num() > 0)
                    {
                        const uint16 SourceRow = Row - RowsToMerge;
                        const uint16 SourceCol = Col - ColumnsToMerge;
                        const uint16 SourceIndex = SourceRow * 16 + SourceCol;
                        OutArray[DestIndex] = BaseChunks[SourceIndex];
                    }
                    else
                    {
                        OutArray[DestIndex] = Chunk();
                    }
                }
            }
        }
        return OutArray;
    }
}