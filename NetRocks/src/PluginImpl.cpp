#include "Globals.h"
#include <KeyFileHelper.h>
#include "PluginImpl.h"
#include "Host/HostLocal.h"
#include "UI/SiteConnectionEditor.h"
#include "Op/OpConnect.h"
#include "Op/OpGetMode.h"
#include "Op/OpXfer.h"
#include "Op/OpRemove.h"
#include "Op/OpMakeDirectory.h"
#include "Op/OpEnumDirectory.h"

PluginImpl::PluginImpl(const char *path)
{
	_cur_dir[0] = _panel_title[0] = 0;
	_local.reset((IHost *)new HostLocal());
	UpdatePanelTitle();
}

PluginImpl::~PluginImpl()
{
}

void PluginImpl::UpdatePanelTitle()
{
	std::string tmp;
	if (_remote) {
//		tmp = _remote->SiteInfo();
//		tmp+= '/';
		tmp+= _cur_dir;
	} else {
		tmp = "NetRocks connections list";
	}

	if (tmp.size() >= sizeof(_panel_title)) {
		size_t rmlen = 4 + (tmp.size() - sizeof(_panel_title));
		tmp.replace((tmp.size() - rmlen) / 2 , rmlen, "...");
	}
	
	memcpy(_panel_title, tmp.c_str(), tmp.size() + 1);
}

bool PluginImpl::ValidateConnection()
{
	try {
		if (!_remote)
			return false;

		if (_remote->IsBroken()) 
			throw std::runtime_error("Connection broken");

	} catch (std::exception &e) {
		fprintf(stderr, "NetRocks::ValidateConnection: %s\n", e.what());
		_remote.reset();
		return false;
	}

	return true;
}

std::string PluginImpl::CurrentSiteDir(bool with_ending_slash) const
{
	std::string out;
	const char *slash = strchr(_cur_dir, '/');
	if (slash) {
		out = slash + 1;
	}

	if (out.empty()) {
		out = with_ending_slash ? "./" : ".";

	} else if (out[out.size() - 1] != '/') {
		if (with_ending_slash)
			out+= "/";

	} else if (!with_ending_slash) {
		out.resize(out.size() - 1);
		if (out.empty())
			out = ".";
	}
	return out;
}

int PluginImpl::GetFindData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
	fprintf(stderr, "NetRocks::GetFindData '%s' G.config='%s'\n", _cur_dir, G.config.c_str());
	FP_SizeItemList il(FALSE);
	try {
		PluginPanelItem tmp = {};
		if (!ValidateConnection()) {
			_cur_dir[0] = 0;
			KeyFileHelper kfh(G.config.c_str());
			const std::vector<std::string> &sites = kfh.EnumSections();
			for (const auto &site : sites) {
				strncpy(tmp.FindData.cFileName, site.c_str(), sizeof(tmp.FindData.cFileName));
				tmp.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
				if (!il.Add(&tmp))
					throw std::runtime_error("Can't add list entry");
			}

		} else {
			strncpy(tmp.FindData.cFileName, "..", sizeof(tmp.FindData.cFileName) - 1);
			tmp.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
			if (!il.Add(&tmp))
				throw std::runtime_error("Can't add list entry");

			OpEnumDirectory(_remote, OpMode, CurrentSiteDir(false), il).Do();
			//_remote->DirectoryEnum(CurrentSiteDir(false), il, OpMode);
		}

	} catch (std::exception &e) {
		fprintf(stderr, "NetRocks::GetFindData('%s', %d) ERROR: %s\n", _cur_dir, OpMode, e.what());
		il.Free(il.Items(), il.Count() );
		*pPanelItem = nullptr;
		*pItemsNumber = 0;
		return FALSE;
	}

	*pPanelItem = il.Items();
	*pItemsNumber = il.Count();
	return TRUE;
}

void PluginImpl::FreeFindData(PluginPanelItem *PanelItem, int ItemsNumber)
{
	FP_SizeItemList::Free(PanelItem, ItemsNumber);
}

int PluginImpl::SetDirectory(const char *Dir, int OpMode)
{
	fprintf(stderr, "NetRocks::SetDirectory('%s', %d)\n", Dir, OpMode);
	if (strcmp(Dir, ".") == 0)
		return TRUE;

	try {
		std::string tmp = _cur_dir;

		if (strcmp(Dir, "..") == 0) {
			size_t p = tmp.rfind('/');
			if (p == std::string::npos)
				p = 0;
			tmp.resize(p);

		} else if (*Dir == '/') {
			tmp = Dir + 1;

		} else {
			if (!tmp.empty())
				tmp+= '/';
			tmp+= Dir;
		}
		while (!tmp.empty() && tmp[tmp.size() - 1] == '/')
			tmp.resize(tmp.size() - 1);

		size_t p = tmp.find('/');
		if (!_remote) {
			const std::string &site = tmp.substr(0, p);
			_remote = OpConnect(OpMode, site).Do();
			if (!_remote) {
				return FALSE;
			}

//			if (p != std::string::npos)
//				tmp.erase(0, p + 1);
//			else
//				tmp.clear();
		}

		if (p != std::string::npos && p < tmp.size() - 1) {
			mode_t mode = 0;
			if (!OpGetMode(_remote, OpMode, tmp.substr(p + 1)).Do(mode))
				throw std::runtime_error("Get mode failed");

			if (!S_ISDIR(mode))
				throw std::runtime_error("Not a directory");
		}

		if (tmp.size() >= sizeof(_cur_dir))
			throw std::runtime_error("Too long path");

		memcpy(_cur_dir, tmp.c_str(), tmp.size() + 1);

	} catch (std::exception &e) {
		fprintf(stderr, "NetRocks::SetDirectory('%s', %d) ERROR: %s\n", Dir, OpMode, e.what());
		return FALSE;
	}

	UpdatePanelTitle();

	fprintf(stderr, "NetRocks::SetDirectory('%s', %d) OK: '%s'\n", Dir, OpMode, _cur_dir);

	return TRUE;
}

void PluginImpl::GetOpenPluginInfo(struct OpenPluginInfo *Info)
{
	fprintf(stderr, "NetRocks::GetOpenPluginInfo: '%s' \n", _cur_dir);
//	snprintf(_panel_title, ARRAYSIZE(_panel_title),
//	          " Inside: %s@%s ", _dir.c_str(), _name.c_str());

	Info->Flags = OPIF_SHOWPRESERVECASE | OPIF_USEHIGHLIGHTING;
	Info->HostFile = NULL;
	Info->CurDir = _cur_dir;
	Info->PanelTitle = _panel_title;
}


int PluginImpl::GetFiles(struct PluginPanelItem *PanelItem, int ItemsNumber, int Move, char *DestPath, int OpMode)
{
	fprintf(stderr, "NetRocks::GetFiles: _dir='%s' DestPath='%s' ItemsNumber=%d\n", _cur_dir, DestPath, ItemsNumber);
	if (ItemsNumber <= 0)
		return FALSE;

	std::string dst_dir;
	if (DestPath)
		dst_dir = DestPath;

	return OpXfer(_remote, OpMode, CurrentSiteDir(true), _local, dst_dir,
		PanelItem, ItemsNumber, Move ? XK_MOVE : XK_COPY, XK_DOWNLOAD).Do() ? TRUE : FALSE;
}

int PluginImpl::PutFiles(struct PluginPanelItem *PanelItem, int ItemsNumber, int Move, int OpMode)
{
	const std::string &site_dir = CurrentSiteDir(true);
	fprintf(stderr, "NetRocks::GetFiles: _dir='%s' site_dir='%s' ItemsNumber=%d\n", _cur_dir, site_dir.c_str(), ItemsNumber);
	if (ItemsNumber <= 0)
		return FALSE;

	char dst_dir[PATH_MAX] = {};
	sdc_getcwd(dst_dir, PATH_MAX-2);
	size_t l = strlen(dst_dir);
	if (l == 0) {
		strcpy(dst_dir, "./");

	} else if (dst_dir[l - 1] != '/') {
		dst_dir[l] = '/';
		dst_dir[l + 1] = 0;
	}

	return OpXfer(_local, OpMode, dst_dir, _remote, CurrentSiteDir(true),
		PanelItem, ItemsNumber, Move ? XK_MOVE : XK_COPY, XK_UPLOAD).Do() ? TRUE : FALSE;
}

int PluginImpl::DeleteFiles(struct PluginPanelItem *PanelItem, int ItemsNumber, int OpMode)
{
	fprintf(stderr, "NetRocks::DeleteFiles: _dir='%s' ItemsNumber=%d\n", _cur_dir, ItemsNumber);
	if (ItemsNumber <= 0)
		return FALSE;

	return OpRemove(_remote, OpMode, CurrentSiteDir(true), PanelItem, ItemsNumber).Do() ? TRUE : FALSE;
}

int PluginImpl::MakeDirectory(const char *Name, int OpMode)
{
	fprintf(stderr, "NetRocks::MakeDirectory('%s', 0x%x)\n", Name, OpMode);
	if (_cur_dir[0]) {
		return OpMakeDirectory(_remote, OpMode, CurrentSiteDir(true), Name).Do();
	} else {
		;//todo
	}
	return FALSE;
}

int PluginImpl::ProcessKey(int Key, unsigned int ControlState)
{
	fprintf(stderr, "NetRocks::ProcessKey(0x%x, 0x%x)\n", Key, ControlState);

	if (!_cur_dir[0] && Key==VK_F4
	&& (ControlState == 0 || ControlState == PKF_SHIFT))
	{
		std::string site;

		if (ControlState == 0) {
			PanelInfo pi = {};
			G.info.Control(this, FCTL_GETPANELINFO, &pi);

			if (pi.CurrentItem >= pi.ItemsNumber)
				return TRUE;

			site = pi.PanelItems[pi.CurrentItem].FindData.cFileName;
		}
		SiteConnectionEditor sce(site);
		const bool connect_now = sce.Edit();
		G.info.Control(this, FCTL_UPDATEPANEL, NULL);
		if (connect_now) {
			SetDirectory(sce.DisplayName().c_str(), 0);
		}

		return TRUE;
	}
/*
	if (Key == VK_F7) {
		MakeDirectory();
		return TRUE;
	}
*/
	return FALSE;
}
