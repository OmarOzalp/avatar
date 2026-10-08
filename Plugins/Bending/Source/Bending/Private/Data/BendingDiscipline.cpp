#include "Data/BendingDiscipline.h"

const FPrimaryAssetType UBendingDiscipline::PrimaryAssetType(TEXT("BendingDiscipline"));

FPrimaryAssetId UBendingDiscipline::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}
