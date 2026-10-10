/** [INPUT]: Director-owned continuous reveal field. [OUTPUT]: B-C triangular interpolation shared with Slate, reveal threshold .45.
 * [POS]: Minimap gameplay visibility contract, no Slate state. [PROTOCOL]: Keep module AGENTS synchronized. */
#include "AI/WaveDirector.h"
bool AWaveDirector::IsExplored(float WorldX, float WorldY) const
{
	if (!bFogInitialized) { return true; }
	const float X = FMath::Clamp((WorldX+FogWorldHalfExtent)/(2.f*FogWorldHalfExtent)*(FogGridN-1),0.f,float(FogGridN-1));
	const float Y = FMath::Clamp((WorldY+FogWorldHalfExtent)/(2.f*FogWorldHalfExtent)*(FogGridN-1),0.f,float(FogGridN-1));
	const int32 X0=FMath::FloorToInt(X), Y0=FMath::FloorToInt(Y);
	const int32 X1=FMath::Min(X0+1,FogGridN-1), Y1=FMath::Min(Y0+1,FogGridN-1);
	const float A=ExploredGrid[Y0*FogGridN+X0], B=ExploredGrid[Y0*FogGridN+X1];
	const float C=ExploredGrid[Y1*FogGridN+X0], D=ExploredGrid[Y1*FogGridN+X1];
	const float Tx=X-X0, Ty=Y-Y0;
	const float Reveal=Tx+Ty<=1.f ? A*(1.f-Tx-Ty)+B*Tx+C*Ty
		: D*(Tx+Ty-1.f)+B*(1.f-Ty)+C*(1.f-Tx);
	return Reveal>=0.45f;
}
