#include "WindowsUpdate.h"
#include <General/util/StrUtil.h>
using namespace zzj::WindowsUpdate;

std::tuple<int, std::vector<UpdateInfo>> UpdateInfo::GetUpdateInfo(const std::string &criteria)
{
    HRESULT hr;
    hr = CoInitialize(NULL);
    if (FAILED(hr))
        return {-1, {}};
    auto deleter = [](auto hr) { CoUninitialize(); };

    std::unique_ptr<HRESULT,decltype(deleter)> hrPtr(&hr, deleter);
    CComPtr<IUpdateSession> iUpdate;
    CComPtr<IUpdateSearcher> searcher;
    CComPtr<ISearchResult> results;
    std::vector<UpdateInfo> ret;
    BSTR bStrCriteria = SysAllocString(str::utf82w(criteria).c_str());

    hr = CoCreateInstance(CLSID_UpdateSession, NULL, CLSCTX_INPROC_SERVER, IID_IUpdateSession, (LPVOID *)&iUpdate);
    if (FAILED(hr))
    {
        return {-2, {}};
    }
    hr = iUpdate->CreateUpdateSearcher(&searcher);
    if (FAILED(hr))
    {
        return {-3, {}};
    }

    hr = searcher->Search(bStrCriteria, &results);
    if (FAILED(hr))
    {
        return {-4, {}};
    }
    SysFreeString(bStrCriteria);
    CComPtr<IUpdateCollection> updateList;
    LONG updateSize;
    LONG totalKB = 0;
    results->get_Updates(&updateList);
    updateList->get_Count(&updateSize);

    for (LONG i = 0; i < updateSize; i++)
    {
        CComPtr<IUpdate> updateItem;
        updateList->get_Item(i, &updateItem);
        UpdateInfo info = GetUpdateInfoFromUpdateItem(updateItem);

        CComPtr<IUpdateCollection> updtCollection;
        LONG updtBundledCount;

        // Retrieve the bundled updates
        updateItem->get_BundledUpdates(&updtCollection);
        hr = updtCollection->get_Count(&updtBundledCount);
        if ((updtBundledCount > 0) && (hr == S_OK))
        {
            for (LONG j = 0; j < updtBundledCount; j++)
            {
                CComPtr<IUpdate> bundledUpdateItem;
                updtCollection->get_Item(j, &bundledUpdateItem);
                UpdateInfo bundleInfo = GetUpdateInfoFromUpdateItem(bundledUpdateItem);
                info.bundledUpdates.push_back(bundleInfo);
            }
        }
        ret.push_back(info);
    }
    return {0, ret};
}
UpdateInfo UpdateInfo::GetUpdateInfoFromUpdateItem(CComPtr<IUpdate> updateItem)
{
    UpdateInfo info;
    CComPtr<IStringCollection> KBCollection;
    BSTR strTmp;
    VARIANT_BOOL boolTmp;
    VARIANT variantTmp;
    VariantInit(&variantTmp);
    LONG KBCount;
    int totalKB = 0;

    auto hr = updateItem->get_Title(&strTmp);
    if (!FAILED(hr) && strTmp != nullptr)
    {
        info.title = str::w2utf8(strTmp);
        ::SysFreeString(strTmp);
    }
    hr = updateItem->get_Description(&strTmp);
    if (!FAILED(hr) && strTmp != nullptr)
    {
        info.description = str::w2utf8(strTmp);
        ::SysFreeString(strTmp);
    }

    hr = updateItem->get_AutoSelectOnWebSites(&boolTmp);
    if (!FAILED(hr))
        info.autoSeleteOnWebSites = boolTmp;

    hr = updateItem->get_CanRequireSource(&boolTmp);
    if (!FAILED(hr))
        info.canRequireSource = boolTmp;

    hr = updateItem->get_IsDownloaded(&boolTmp);
    if (!FAILED(hr))
        info.isDownloaded = boolTmp;

    hr = updateItem->get_IsHidden(&boolTmp);
    if (!FAILED(hr))
        info.isHidden = boolTmp;

    hr = updateItem->get_IsInstalled(&boolTmp);
    if (!FAILED(hr))
        info.isInstalled = boolTmp;

    hr = updateItem->get_IsMandatory(&boolTmp);
    if (!FAILED(hr))
        info.isMandatory = boolTmp;

    hr = updateItem->get_IsUninstallable(&boolTmp);
    if (!FAILED(hr))
        info.isMandatory = boolTmp;

    hr = updateItem->get_Type(&info.type);

    hr = updateItem->get_Deadline(&variantTmp);
    if (!FAILED(hr))
    {
        if (variantTmp.vt == VT_DATE)
            info.deadLine = variantTmp.date;
        VariantClear(&variantTmp);
    }

    updateItem->get_KBArticleIDs(&KBCollection);
    KBCollection->get_Count(&KBCount);
    for (int i = 0; i < KBCount; i++)
    {
        BSTR KBValue;
        totalKB += 1;
        KBCollection->get_Item(i, &KBValue);
        if(KBValue!= nullptr)
            info.kbArticleIDs.push_back(str::w2utf8(KBValue));
        ::SysFreeString(KBValue);
    }
    return info;
}
