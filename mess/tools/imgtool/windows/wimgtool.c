//============================================================
//
//	wimgtool.c - Win32 GUI Imgtool
//
//============================================================

#include <windows.h>
#include <commctrl.h>
#include <tchar.h>

#include "wimgtool.h"
#include "wimgres.h"
#include "pile.h"
#include "strconv.h"

const TCHAR wimgtool_class[] = TEXT("wimgtoolclass");

struct wimgtool_info
{
	HWND listview;
	IMAGE *image;
	HIMAGELIST iconlist_normal;
	HIMAGELIST iconlist_small;
	mess_pile iconlist_extensions;
};



static void nyi(HWND window)
{
	MessageBox(window, TEXT("Not yet implemented"), NULL, MB_OK);
}



static struct wimgtool_info *get_wimgtool_info(HWND window)
{
	struct wimgtool_info *info;
	LONG_PTR l;
	l = GetWindowLongPtr(window, GWLP_USERDATA);
	info = (struct wimgtool_info *) l;
	return info;
}



static imgtoolerr_t get_selected_dirent(HWND window, imgtool_dirent *entry)
{
	struct wimgtool_info *info;
	int selected_item;
	IMAGEENUM *imageenum = NULL;
	imgtoolerr_t err;
	
	info = get_wimgtool_info(window);
	if (!info->image)
	{
		err = IMGTOOLERR_UNEXPECTED;
		goto done;
	}
	selected_item = ListView_GetNextItem(info->listview, -1, LVIS_SELECTED | LVIS_FOCUSED);
	if (selected_item < 0)
	{
		err = IMGTOOLERR_UNEXPECTED;
		goto done;
	}

	err = img_beginenum(info->image, &imageenum);
	if (err)
		goto done;
	do
	{
		err = img_nextenum(imageenum, entry);
		if (err)
			goto done;
		if (entry->eof)
		{
			err = IMGTOOLERR_FILENOTFOUND;
			goto done;
		}
	}
	while(selected_item--);

done:
	if (err)
		memset(entry->fname, 0, entry->fname_len);
	if (imageenum)
		img_closeenum(imageenum);
	return err;
}



static void report_error(HWND window, imgtoolerr_t err)
{
	MessageBox(window, imgtool_error(err), NULL, MB_OK);
}



static int append_associated_icon(HWND window, const char *extension)
{
	HICON icon;
	WORD icon_index;
	TCHAR icon_path[MAX_PATH];
	int index = -1;
	struct wimgtool_info *info;

	info = get_wimgtool_info(window);

	_tcscpy(icon_path, TEXT("nul"));
	if (extension)
		_tcscat(icon_path, A2T(extension));

	icon = ExtractAssociatedIcon(NULL, icon_path, &icon_index);
	if (icon)
	{
		index = ImageList_AddIcon(info->iconlist_normal, icon);
		ImageList_AddIcon(info->iconlist_small, icon);
		DestroyIcon(icon);
	}
	return index;
}



static imgtoolerr_t append_dirent(HWND window, const imgtool_dirent *entry)
{
	LVITEM lvi;
	int new_index;
	struct wimgtool_info *info;
	TCHAR buffer[32];
	int icon_index;
	const char *extension;
	const char *ptr;
	size_t size, i;

	info = get_wimgtool_info(window);
	extension = strchr(entry->fname, '.');
	if (!extension)
		extension = ".bin";

	ptr = pile_getptr(&info->iconlist_extensions);
	size = pile_size(&info->iconlist_extensions);
	icon_index = 0;
	for (i = 0; i < size; i += strlen(&ptr[i]) + 1)
	{
		if (!stricmp(&ptr[i], extension))
			break;
		icon_index++;
	}

	if (i >= size)
	{
		icon_index = append_associated_icon(window, extension);
		if (icon_index < 0)
			return IMGTOOLERR_UNEXPECTED;
		if (pile_puts(&info->iconlist_extensions, extension))
			return IMGTOOLERR_OUTOFMEMORY;
		if (pile_putc(&info->iconlist_extensions, '\0'))
			return IMGTOOLERR_OUTOFMEMORY;
	}

	memset(&lvi, 0, sizeof(lvi));
	lvi.iItem = ListView_GetItemCount(info->listview);
	lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
	lvi.pszText = entry->fname;
	lvi.iImage = icon_index;
	new_index = ListView_InsertItem(info->listview, &lvi);

	_sntprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
		TEXT("%d"), entry->filesize);
	ListView_SetItemText(info->listview, new_index, 1, buffer);

	return 0;
}



static imgtoolerr_t refresh_image(HWND window)
{
	imgtoolerr_t err;
	IMAGEENUM *imageenum = NULL;
	char enumfilename[256];
	imgtool_dirent entry;
	struct wimgtool_info *info;

	info = get_wimgtool_info(window);

	ListView_DeleteAllItems(info->listview);

	err = img_beginenum(info->image, &imageenum);
	if (err)
		goto done;

	memset(&entry, 0, sizeof(entry));
	entry.fname = enumfilename;
	entry.fname_len = sizeof(enumfilename) / sizeof(enumfilename[0]);
	do
	{
		err = img_nextenum(imageenum, &entry);
		if (err)
			goto done;

		if (entry.fname[0])
		{
			err = append_dirent(window, &entry);
			if (err)
				goto done;
		}
	}
	while(!entry.eof);

done:
	if (imageenum)
		img_closeenum(imageenum);
	return err;
}



static void menu_open(HWND window)
{
	OPENFILENAME ofn;
	const struct ImageModule *module = NULL;
	mess_pile pile;
	const char *s;
	imgtoolerr_t err = 0;
	IMAGE *image;
	TCHAR buffer[MAX_PATH] = { 0 };
	struct wimgtool_info *info;

	info = get_wimgtool_info(window);
	pile_init(&pile);

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = window;
	ofn.lpstrFile = buffer;
	ofn.nMaxFile = sizeof(buffer) / sizeof(buffer[0]);

	while((module = imgtool_library_iterate(library, module)) != NULL)
	{
		pile_puts(&pile, module->description);
		pile_puts(&pile, " (");

		s = module->extensions;
		while(*s)
		{
			if (s != module->extensions)
				pile_putc(&pile, ';');
			pile_printf(&pile, "*.%s", s);
			s += strlen(s) + 1;
		}
		pile_putc(&pile, ')');
		pile_putc(&pile, '\0');

		s = module->extensions;
		while(*s)
		{
			if (s != module->extensions)
				pile_putc(&pile, ';');
			pile_printf(&pile, "*.%s", s);
			s += strlen(s) + 1;
		}

		pile_putc(&pile, '\0');
	}
	pile_putc(&pile, '\0');
	pile_putc(&pile, '\0');

	ofn.lpstrFilter = pile_detach(&pile);

	if (!GetOpenFileName(&ofn))
		goto done;

	module = imgtool_library_index(library, ofn.nFilterIndex - 1);
	err = img_open(module, ofn.lpstrFile, (ofn.Flags & OFN_READONLY)
		? OSD_FOPEN_READ : OSD_FOPEN_RW, &image);
	if (err)
		goto done;

	if (info->image)
		img_close(info->image);
	info->image = image;

	SetWindowText(window, ofn.lpstrFile);
	refresh_image(window);

done:
	if (err)
		report_error(window, err);
	pile_delete(&pile);
}



static void menu_insert(HWND window)
{
	imgtoolerr_t err;
	const char *image_filename;
	TCHAR host_filename[MAX_PATH] = { 0 };
	const TCHAR *s;
	OPENFILENAME ofn;
	struct wimgtool_info *info;

	info = get_wimgtool_info(window);

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFile = host_filename;
	ofn.nMaxFile = sizeof(host_filename) / sizeof(host_filename[0]);
	if (!GetOpenFileName(&ofn))
	{
		err = 0;
		goto done;
	}

	s = _tcsrchr(ofn.lpstrFile, '\\');
	s = s ? s + 1 : ofn.lpstrFile;
	image_filename = T2A(s);

	err = img_putfile(info->image, NULL, ofn.lpstrFile, NULL, NULL);
	if (err)
		goto done;

	err = refresh_image(window);
	if (err)
		goto done;

done:
	if (err)
		report_error(window, err);
}



static void menu_extract(HWND window)
{
	imgtoolerr_t err;
	imgtool_dirent entry;
	char image_filename[MAX_PATH];
	TCHAR host_filename[MAX_PATH];
	OPENFILENAME ofn;
	struct wimgtool_info *info;

	info = get_wimgtool_info(window);

	memset(&entry, 0, sizeof(entry));
	entry.fname = image_filename;
	entry.fname_len = sizeof(image_filename) / sizeof(image_filename[0]);
	err = get_selected_dirent(window, &entry);
	if (err)
		goto done;

	strcpy(host_filename, image_filename);

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFile = host_filename;
	ofn.nMaxFile = sizeof(host_filename) / sizeof(host_filename[0]);
	if (!GetSaveFileName(&ofn))
		goto done;

	err = img_getfile(info->image, entry.fname, ofn.lpstrFile, NULL);
	if (err)
		goto done;

done:
	if (err)
		report_error(window, err);
}



static void menu_delete(HWND window)
{
	imgtoolerr_t err;
	imgtool_dirent entry;
	char image_filename[MAX_PATH];
	struct wimgtool_info *info;

	info = get_wimgtool_info(window);

	memset(&entry, 0, sizeof(entry));
	entry.fname = image_filename;
	entry.fname_len = sizeof(image_filename) / sizeof(image_filename[0]);
	err = get_selected_dirent(window, &entry);
	if (err)
		goto done;

	err = img_deletefile(info->image, image_filename);
	if (err)
		goto done;

	err = refresh_image(window);
	if (err)
		goto done;

done:
	if (err)
		report_error(window, err);
}



static void set_listview_style(HWND window, DWORD style)
{
	struct wimgtool_info *info;

	info = get_wimgtool_info(window);
	style &= LVS_TYPEMASK;
	style |= (GetWindowLong(info->listview, GWL_STYLE) & ~LVS_TYPEMASK);
	SetWindowLong(info->listview, GWL_STYLE, style);
}



static LRESULT wimgtool_create(HWND window, CREATESTRUCT *pcs)
{
	LVCOLUMN col;
	struct wimgtool_info *info;

	info = malloc(sizeof(*info));
	if (!info)
		return -1;
	memset(info, 0, sizeof(*info));
	pile_init(&info->iconlist_extensions);

	SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR) info);

	SetWindowText(window, TEXT("MESS Image Tool"));

	// create the list view
	info->listview = CreateWindow(WC_LISTVIEW, NULL,
		WS_VISIBLE | WS_CHILD, 0, 0, pcs->cx, pcs->cy, window, NULL, NULL, NULL);
	if (!info->listview)
		return -1;
	set_listview_style(window, LVS_REPORT);

	col.mask = LVCF_TEXT | LVCF_WIDTH;
	col.pszText = (LPTSTR) TEXT("Filename");
	col.cx = 100;
	if (ListView_InsertColumn(info->listview, 0, &col) < 0)
		return -1;
	col.pszText = (LPTSTR) TEXT("Size");
	if (ListView_InsertColumn(info->listview, 1, &col) < 0)
		return -1;

	info->iconlist_normal = ImageList_Create(32, 32, ILC_COLOR, 0, 0);
	info->iconlist_small = ImageList_Create(16, 16, ILC_COLOR, 0, 0);
	if (!info->iconlist_normal || !info->iconlist_small)
		return -1;
	ListView_SetImageList(info->listview, info->iconlist_normal, LVSIL_NORMAL);
	ListView_SetImageList(info->listview, info->iconlist_small, LVSIL_SMALL);

	return 0;
}



static LRESULT CALLBACK wimgtool_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	struct wimgtool_info *info;
	RECT r;

	info = get_wimgtool_info(window);

	switch(message)
	{
		case WM_CREATE:
			if (wimgtool_create(window, (CREATESTRUCT *) lparam))
				return -1;
			break;

		case WM_DESTROY:
			if (info)
			{
				if (info->image)
					img_close(info->image);
				pile_delete(&info->iconlist_extensions);
				free(info);
			}
			break;

		case WM_SIZE:
			GetClientRect(window, &r);
			SetWindowPos(info->listview, NULL, 0, 0, r.right - r.left,
				r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
			break;

		case WM_INITMENU:
			EnableMenuItem((HMENU) wparam, ID_IMAGE_INSERT,
				MF_BYCOMMAND | (info->image ? MF_ENABLED : MF_GRAYED));
			EnableMenuItem((HMENU) wparam, ID_IMAGE_EXTRACT,
				MF_BYCOMMAND | (info->image ? MF_ENABLED : MF_GRAYED));
			EnableMenuItem((HMENU) wparam, ID_IMAGE_DELETE,
				MF_BYCOMMAND | (info->image ? MF_ENABLED : MF_GRAYED));
			break;

		case WM_COMMAND:
			switch(LOWORD(wparam))
			{
				case ID_FILE_NEW:
					nyi(window);
					break;

				case ID_FILE_OPEN:
					menu_open(window);
					break;

				case ID_FILE_CLOSE:
					PostMessage(window, WM_CLOSE, 0, 0);
					break;

				case ID_IMAGE_INSERT:
					menu_insert(window);
					break;

				case ID_IMAGE_EXTRACT:
					menu_extract(window);
					break;

				case ID_IMAGE_DELETE:
					menu_delete(window);
					break;

				case ID_VIEW_ICONS:
					set_listview_style(window, LVS_ICON);
					break;

				case ID_VIEW_LIST:
					set_listview_style(window, LVS_LIST);
					break;

				case ID_VIEW_DETAILS:
					set_listview_style(window, LVS_REPORT);
					break;

			}
			break;
	}

	return DefWindowProc(window, message, wparam, lparam);
}



BOOL wimgtool_registerclass(void)
{
	WNDCLASS wimgtool_wndclass;

	memset(&wimgtool_wndclass, 0, sizeof(wimgtool_wndclass));
	wimgtool_wndclass.lpfnWndProc = wimgtool_wndproc;
	wimgtool_wndclass.lpszClassName = wimgtool_class;
	wimgtool_wndclass.lpszMenuName = MAKEINTRESOURCE(IDR_WIMGTOOL_MENU);
	return RegisterClass(&wimgtool_wndclass);
}



