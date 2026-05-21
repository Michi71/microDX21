// ui_widget_factory_mono.cpp (Skeleton)
#include "ui_widget_factory.h"
#include "ui_widgets_mono.h"

IHeader* UIWidgetFactory::CreateHeader(DisplayType type)
{
    switch (type) {
    case DisplayType::Mono128x64:
    case DisplayType::Mono128x32:
        return new MonoHeader();
    default:
        return nullptr;
    }
}

IListView* UIWidgetFactory::CreateList(DisplayType type)
{
    switch (type)
    {
        case DisplayType::Mono128x32:
        case DisplayType::Mono128x64:
            return new MonoListView();

        // später: ColorListView etc.
        default:
            return new MonoListView();
    }
}

IParamControl* UIWidgetFactory::CreateParam(DisplayType type)
{
    switch (type) {
    case DisplayType::Mono128x64:
    case DisplayType::Mono128x32:
        return new MonoParamControl();
    default:
        return nullptr;
    }
}
