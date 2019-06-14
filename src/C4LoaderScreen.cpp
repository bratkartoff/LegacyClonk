/*
 * LegacyClonk
 *
 * Copyright (c) RedWolf Design
 * Copyright (c) 2003, Sven2
 * Copyright (c) 2017-2019, The LegacyClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

// startup screen

#include <C4Include.h>
#include <C4LoaderScreen.h>

#ifndef BIG_C4INCLUDE
#include <C4LogBuf.h>
#include <C4Log.h>
#include <C4Game.h>
#include <C4Random.h>
#include <C4GroupSet.h>
#endif

C4LoaderScreen::C4LoaderScreen() : TitleFont(Game.GraphicsResource.FontTitle), LogFont(Game.GraphicsResource.FontTiny)
{
	// zero fields
	szInfo = nullptr;
}

C4LoaderScreen::~C4LoaderScreen()
{
	// clear fields
	delete[] szInfo;
}

bool C4LoaderScreen::Init(std::string loaderSpec)
{
	loaders.clear();

	C4Group *group = nullptr;
	while ((group = Game.GroupSet.FindGroup(C4GSCnt_Loaders, group, true)))
	{
		SeekLoaderScreens(*group, loaderSpec);
	}

	// nothing found? seek in main gfx grp
	C4Group GfxGrp;
	if (!loaders.size())
	{
		// open it
		if (!GfxGrp.Open(Config.AtExePath(C4CFN_Graphics)))
		{
			LogFatal(FormatString(LoadResStr("IDS_PRC_NOGFXFILE"), C4CFN_Graphics, GfxGrp.GetError()).getData());
			return false;
		}
		SeekLoaderScreens(GfxGrp, loaderSpec);
		// Still nothing found: fall back to general loader spec in main graphics group
		if (!loaders.size() && loaderSpec != DefaultSpec)
		{
			SeekLoaderScreens(GfxGrp, DefaultSpec);
		}
		// Not even default loaders available? Fail.
		if (!loaders.size())
		{
			LogFatal(FormatString("No loaders found for loader specification %s", loaderSpec.c_str()).getData());
			return false;
		}
	}

	// load loader
	fctBackground.GetFace().SetBackground();
	const auto &pair = loaders.at(static_cast<size_t>(SafeRandom(static_cast<int>(loaders.size()))));
	if (!fctBackground.Load(*pair.second, pair.first.c_str(), C4FCT_Full, C4FCT_Full, true))
	{
		return false;
	}

	// load info
	delete[] szInfo; szInfo = nullptr;

	// init fonts
	if (!Game.GraphicsResource.InitFonts())
		return false;

	// initial draw
	C4Facet cgo;
	cgo.Set(Application.DDraw->lpPrimary, 0, 0, Config.Graphics.ResX, Config.Graphics.ResY);
	Draw(cgo);

	// done, success!
	return true;
}

void C4LoaderScreen::SeekLoaderScreens(C4Group &group, const std::string &wildcard)
{
	for (const auto &extension : {".jpeg", ".jpg", ".png", ".bmp"})
	{
		std::string pattern(wildcard);
		pattern.append(extension);

		char filename[_MAX_PATH + 1];
		for (bool found = group.FindEntry(pattern.c_str(), filename); found; found = group.FindNextEntry(pattern.c_str(), filename))
		{
			loaders.push_back(std::make_pair(filename, &group));
		}
	}
}

void C4LoaderScreen::Draw(C4Facet &cgo, int iProgress, C4LogBuffer *pLog, int Process)
{
	// cgo.X/Y is assumed 0 here...
	// fixed positions for now
	int iHIndent = 20;
	int iVIndent = 20;
	int iLogBoxHgt = 84;
	int iLogBoxMargin = 2;
	int iVMargin = 5;
	int iProgressBarHgt = 15;
	CStdFont &rLogBoxFont = LogFont, &rProgressBarFont = Game.GraphicsResource.FontRegular;
	float fLogBoxFontZoom = 1.0f;
	// Background (loader)
	fctBackground.DrawFullScreen(cgo);
	// draw scenario title
	Application.DDraw->StringOut(Game.ScenarioTitle.getData(), TitleFont, 1.0f, cgo.Surface, cgo.Wdt - iHIndent, cgo.Hgt - iVIndent - iLogBoxHgt - iVMargin - iProgressBarHgt - iVMargin - TitleFont.iLineHgt, 0xdddddddd, ARight, false);
	// draw progress bar
	Application.DDraw->DrawBoxDw(cgo.Surface, iHIndent, cgo.Hgt - iVIndent - iLogBoxHgt - iVMargin - iProgressBarHgt, cgo.Wdt - iHIndent, cgo.Hgt - iVIndent - iLogBoxHgt - iVMargin, 0x4f000000);
	int iProgressBarWdt = cgo.Wdt - iHIndent * 2 - 2;
	if (C4GUI::IsGUIValid())
	{
		C4GUI::GetRes()->fctProgressBar.DrawX(cgo.Surface, iHIndent + 1, cgo.Hgt - iVIndent - iLogBoxHgt - iVMargin - iProgressBarHgt + 1, iProgressBarWdt * iProgress / 100, iProgressBarHgt - 2);
	}
	else
	{
		Application.DDraw->DrawBoxDw(cgo.Surface, iHIndent + 1, cgo.Hgt - iVIndent - iLogBoxHgt - iVMargin - iProgressBarHgt + 1, iHIndent + 1 + iProgressBarWdt * iProgress / 100, cgo.Hgt - iVIndent - iLogBoxHgt - iVMargin - 1, 0x4fff0000);
	}
	sprintf(OSTR, "%i%%", iProgress);
	Application.DDraw->StringOut(OSTR, rProgressBarFont, 1.0f, cgo.Surface, cgo.Wdt / 2, cgo.Hgt - iVIndent - iLogBoxHgt - iVMargin - rProgressBarFont.iLineHgt / 2 - iProgressBarHgt / 2, 0xffffffff, ACenter, true);
	// draw log box
	if (pLog)
	{
		Application.DDraw->DrawBoxDw(cgo.Surface, iHIndent, cgo.Hgt - iVIndent - iLogBoxHgt, cgo.Wdt - iHIndent, cgo.Hgt - iVIndent, 0x7f000000);
		int iLineHgt = int(fLogBoxFontZoom * rLogBoxFont.iLineHgt); if (!iLineHgt) iLineHgt = 5;
		int iLinesVisible = (iLogBoxHgt - 2 * iLogBoxMargin) / iLineHgt;
		int iX = iHIndent + iLogBoxMargin;
		int iY = cgo.Hgt - iVIndent - iLogBoxHgt + iLogBoxMargin;
		int32_t w, h;
		for (int i = -iLinesVisible; i < 0; ++i)
		{
			const char *szLine = pLog->GetLine(i, nullptr, nullptr, nullptr);
			if (!szLine || !*szLine) continue;
			rLogBoxFont.GetTextExtent(szLine, w, h, true);
			lpDDraw->TextOut(szLine, rLogBoxFont, fLogBoxFontZoom, cgo.Surface, iX, iY);
			iY += h;
		}
		// append process text
		if (Process)
		{
			iY -= h; iX += w;
			lpDDraw->TextOut(FormatString("%i%%", (int)Process).getData(), rLogBoxFont, fLogBoxFontZoom, cgo.Surface, iX, iY);
		}
	}
}
