// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "beautifierplugin.h"

#include "beautifierconstants.h"
#include "beautifiertr.h"
#include "generalsettings.h"

#include "artisticstyle/artisticstyle.h"
#include "clangformat/clangformat.h"
#include "uncrustify/uncrustify.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/messagemanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projecttree.h>
#include <texteditor/formattexteditor.h>
#include <texteditor/textdocument.h>
#include <texteditor/textdocumentlayout.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorconstants.h>
#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/mimeutils.h>
#include <utils/process.h>
#include <utils/qtcassert.h>
#include <utils/temporarydirectory.h>
#include <utils/textutils.h>

#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>

using namespace TextEditor;

namespace Beautifier::Internal {

bool isAutoFormatApplicable(const Core::IDocument *document,
                            const QList<Utils::MimeType> &allowedMimeTypes)
{
    if (!document)
        return false;

    if (allowedMimeTypes.isEmpty())
        return true;

    const Utils::MimeType documentMimeType = Utils::mimeTypeForName(document->mimeType());
    return Utils::anyOf(allowedMimeTypes, [&documentMimeType](const Utils::MimeType &mime) {
        return documentMimeType.inherits(mime.name());
    });
}

class BeautifierPluginPrivate : public QObject
{
public:
    BeautifierPluginPrivate();

    void updateActions(Core::IEditor *editor = nullptr);

    void autoFormatOnSave(Core::IDocument *document);

    GeneralSettings generalSettings;

    ArtisticStyle artisticStyleBeautifier;
    ClangFormat clangFormatBeautifier;
    Uncrustify uncrustifyBeautifier;

    BeautifierAbstractTool *m_tools[3] {
        &artisticStyleBeautifier,
        &uncrustifyBeautifier,
        &clangFormatBeautifier
    };
};

static BeautifierPluginPrivate *dd = nullptr;

void BeautifierPlugin::initialize()
{
    Core::ActionContainer *menu = Core::ActionManager::createMenu(Constants::MENU_ID);
    menu->menu()->setTitle(Tr::tr("Bea&utifier"));
    menu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    Core::ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);
}

void BeautifierPlugin::extensionsInitialized()
{
    dd = new BeautifierPluginPrivate;
}

ExtensionSystem::IPlugin::ShutdownFlag BeautifierPlugin::aboutToShutdown()
{
    delete dd;
    dd = nullptr;
    return SynchronousShutdown;
}

BeautifierPluginPrivate::BeautifierPluginPrivate()
{
    for (BeautifierAbstractTool *tool : m_tools)
        generalSettings.autoFormatTools.addOption(tool->id());

    updateActions();

    const Core::EditorManager *editorManager = Core::EditorManager::instance();
    connect(editorManager, &Core::EditorManager::currentEditorChanged,
            this, &BeautifierPluginPrivate::updateActions);
    connect(editorManager, &Core::EditorManager::aboutToSave,
            this, &BeautifierPluginPrivate::autoFormatOnSave);
}

void BeautifierPluginPrivate::updateActions(Core::IEditor *editor)
{
    for (BeautifierAbstractTool *tool : m_tools)
        tool->updateActions(editor);
}

void BeautifierPluginPrivate::autoFormatOnSave(Core::IDocument *document)
{
    if (!generalSettings.autoFormatOnSave.value())
        return;

    if (!isAutoFormatApplicable(document, generalSettings.allowedMimeTypes()))
        return;

    // Check if file is contained in the current project (if wished)
    if (generalSettings.autoFormatOnlyCurrentProject.value()) {
        const ProjectExplorer::Project *pro = ProjectExplorer::ProjectTree::currentProject();
        if (!pro
            || pro->files([document](const ProjectExplorer::Node *n) {
                      return ProjectExplorer::Project::SourceFiles(n)
                             && n->filePath() == document->filePath();
                  })
                   .isEmpty()) {
            return;
        }
    }

    // Find tool to use by id and format file!
    const QString id = generalSettings.autoFormatTools.stringValue();
    auto tool = std::find_if(std::begin(m_tools), std::end(m_tools),
                             [&id](const BeautifierAbstractTool *t){return t->id() == id;});
    if (tool != std::end(m_tools)) {
        if (!(*tool)->isApplicable(document))
            return;
        const TextEditor::Command command = (*tool)->command();
        if (!command.isValid())
            return;
        const QList<Core::IEditor *> editors = Core::DocumentModel::editorsForDocument(document);
        if (editors.isEmpty())
            return;
        if (auto widget = TextEditorWidget::fromEditor(editors.first()))
            TextEditor::formatEditor(widget, command);
    }
}

void BeautifierPlugin::showError(const QString &error)
{
    Core::MessageManager::writeFlashing(Tr::tr("Error in Beautifier: %1").arg(error.trimmed()));
}

QString BeautifierPlugin::msgCannotGetConfigurationFile(const QString &command)
{
    return Tr::tr("Cannot get configuration file for %1.").arg(command);
}

QString BeautifierPlugin::msgFormatCurrentFile()
{
    //: Menu entry
    return Tr::tr("Format &Current File");
}

QString BeautifierPlugin::msgFormatSelectedText()
{
    //: Menu entry
    return Tr::tr("Format &Selected Text");
}

QString BeautifierPlugin::msgFormatAtCursor()
{
    //: Menu entry
    return Tr::tr("&Format at Cursor");
}

QString BeautifierPlugin::msgFormatLines()
{
    //: Menu entry
    return Tr::tr("Format &Line(s)");
}

QString BeautifierPlugin::msgDisableFormattingSelectedText()
{
    //: Menu entry
    return Tr::tr("&Disable Formatting for Selected Text");
}

QString BeautifierPlugin::msgCommandPromptDialogTitle(const QString &command)
{
    //: File dialog title for path chooser when choosing binary
    return Tr::tr("%1 Command").arg(command);
}

} // Beautifier::Internal
