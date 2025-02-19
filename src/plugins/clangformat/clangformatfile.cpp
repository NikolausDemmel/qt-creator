// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangformatfile.h"
#include "clangformatsettings.h"
#include "clangformatutils.h"
#include <cppeditor/cppcodestylesettings.h>
#include <projectexplorer/project.h>
#include <texteditor/tabsettings.h>
#include <utils/qtcassert.h>

#include <sstream>

using namespace ClangFormat;

ClangFormatFile::ClangFormatFile(Utils::FilePath filePath)
    : m_filePath(filePath)
{
    if (!m_filePath.exists()) {
        // create file and folder
        m_filePath.parentDir().createDir();
        std::fstream newStyleFile(m_filePath.path().toStdString(), std::fstream::out);
        if (newStyleFile.is_open()) {
            newStyleFile.close();
        }
        resetStyleToQtC();
        return;
    }

    m_style.Language = clang::format::FormatStyle::LK_Cpp;
    const std::error_code error = clang::format::parseConfiguration(m_filePath.fileContents()
                                                                        .value_or(QByteArray())
                                                                        .toStdString(),
                                                                    &m_style);
    if (error.value() != static_cast<int>(clang::format::ParseError::Success)) {
        resetStyleToQtC();
    }
}

clang::format::FormatStyle ClangFormatFile::style() {
    return m_style;
}

Utils::FilePath ClangFormatFile::filePath()
{
    return m_filePath;
}

void ClangFormatFile::setStyle(clang::format::FormatStyle style)
{
    m_style = style;
    saveNewFormat();
}

bool ClangFormatFile::isReadOnly() const
{
    return m_isReadOnly;
}

void ClangFormatFile::setIsReadOnly(bool isReadOnly)
{
    m_isReadOnly = isReadOnly;
}

void ClangFormatFile::resetStyleToQtC()
{
    m_style = qtcStyle();
    saveStyleToFile(m_style, m_filePath);
}

void ClangFormatFile::setBasedOnStyle(QString styleName)
{
    changeField({"BasedOnStyle", styleName});
    saveNewFormat();
}

QString ClangFormatFile::setStyle(QString style)
{
    const std::error_code error = clang::format::parseConfiguration(style.toStdString(), &m_style);
    if (error.value() != static_cast<int>(clang::format::ParseError::Success)) {
        return QString::fromStdString(error.message());
    }

    saveNewFormat(style.toUtf8());
    return "";
}

QString ClangFormatFile::changeField(Field field)
{
    return changeFields({field});
}

QString ClangFormatFile::changeFields(QList<Field> fields)
{
    std::stringstream content;
    content << "---" << "\n";

    for (const auto &field : fields) {
        content << field.first.toStdString() << ": " << field.second.toStdString() << "\n";
    }

    return setStyle(QString::fromStdString(content.str()));
}

void ClangFormatFile::saveNewFormat()
{
    if (m_isReadOnly)
        return;

    saveStyleToFile(m_style, m_filePath);
}

void ClangFormatFile::saveNewFormat(QByteArray style)
{
    if (m_isReadOnly)
        return;

    m_filePath.writeFileContents(style);
}

void ClangFormatFile::saveStyleToFile(clang::format::FormatStyle style, Utils::FilePath filePath)
{
    std::string styleStr = clang::format::configurationAsText(style);

    // workaround: configurationAsText() add comment "# " before BasedOnStyle line
    const int pos = styleStr.find("# BasedOnStyle");
    if (pos != int(std::string::npos))
        styleStr.erase(pos, 2);
    styleStr.append("\n");
    filePath.writeFileContents(QByteArray::fromStdString(styleStr));
}

CppEditor::CppCodeStyleSettings ClangFormatFile::toCppCodeStyleSettings(
    ProjectExplorer::Project *project) const
{
    using namespace clang::format;
    auto settings = CppEditor::CppCodeStyleSettings::getProjectCodeStyle(project);

    FormatStyle style;
    style.Language = clang::format::FormatStyle::LK_Cpp;
    const std::error_code error
        = parseConfiguration(m_filePath.fileContents().value_or(QByteArray()).toStdString(), &style);
    QTC_ASSERT(error.value() == static_cast<int>(ParseError::Success), return settings);

    // Modifier offset should be opposite to indent width in order indentAccessSpecifiers
    // to be false
    settings.indentAccessSpecifiers = (style.AccessModifierOffset != -1 * int(style.IndentWidth));

    settings.indentNamespaceBody = style.NamespaceIndentation
                                   == FormatStyle::NamespaceIndentationKind::NI_All;
    settings.indentNamespaceBraces = settings.indentNamespaceBody;

    settings.indentClassBraces = style.BreakBeforeBraces == FormatStyle::BS_Whitesmiths;
    settings.indentEnumBraces = settings.indentClassBraces;
    settings.indentBlockBraces = settings.indentClassBraces;
    settings.indentFunctionBraces = settings.indentClassBraces;

    settings.indentSwitchLabels = style.IndentCaseLabels;
#if LLVM_VERSION_MAJOR >= 11
    settings.indentBlocksRelativeToSwitchLabels = style.IndentCaseBlocks;
    settings.indentStatementsRelativeToSwitchLabels = style.IndentCaseBlocks;
    settings.indentControlFlowRelativeToSwitchLabels = style.IndentCaseBlocks;
#endif
    if (style.DerivePointerAlignment
        && ClangFormatSettings::instance().mode() == ClangFormatSettings::Mode::Formatting) {
        settings.bindStarToIdentifier = style.PointerAlignment == FormatStyle::PAS_Right;
        settings.bindStarToTypeName = style.PointerAlignment == FormatStyle::PAS_Left;
        settings.bindStarToLeftSpecifier = style.PointerAlignment == FormatStyle::PAS_Left;
        settings.bindStarToRightSpecifier = style.PointerAlignment == FormatStyle::PAS_Right;
    }

    settings.extraPaddingForConditionsIfConfusingAlign = style.BreakBeforeBinaryOperators
                                                         == FormatStyle::BOS_All;
    settings.alignAssignments = style.BreakBeforeBinaryOperators == FormatStyle::BOS_All
                                || style.BreakBeforeBinaryOperators
                                       == FormatStyle::BOS_NonAssignment;

    return settings;
}

void ClangFormatFile::fromCppCodeStyleSettings(const CppEditor::CppCodeStyleSettings &settings)
{
    using namespace clang::format;
    if (settings.indentAccessSpecifiers)
        m_style.AccessModifierOffset = 0;
    else
        m_style.AccessModifierOffset = -1 * m_style.IndentWidth;

    if (settings.indentNamespaceBody || settings.indentNamespaceBraces)
        m_style.NamespaceIndentation = FormatStyle::NamespaceIndentationKind::NI_All;
    else
        m_style.NamespaceIndentation = FormatStyle::NamespaceIndentationKind::NI_None;

    if (settings.indentClassBraces || settings.indentEnumBraces || settings.indentBlockBraces
        || settings.indentFunctionBraces)
        m_style.BreakBeforeBraces = FormatStyle::BS_Whitesmiths;
    else
        m_style.BreakBeforeBraces = FormatStyle::BS_Custom;


    m_style.IndentCaseLabels = settings.indentSwitchLabels;
#if LLVM_VERSION_MAJOR >= 11
    m_style.IndentCaseBlocks = settings.indentBlocksRelativeToSwitchLabels
                               || settings.indentStatementsRelativeToSwitchLabels
                               || settings.indentControlFlowRelativeToSwitchLabels;
#endif

    if (settings.extraPaddingForConditionsIfConfusingAlign)
        m_style.BreakBeforeBinaryOperators = FormatStyle::BOS_All;
    else if (settings.alignAssignments)
        m_style.BreakBeforeBinaryOperators = FormatStyle::BOS_NonAssignment;
    else
        m_style.BreakBeforeBinaryOperators = FormatStyle::BOS_None;

    m_style.DerivePointerAlignment = settings.bindStarToIdentifier || settings.bindStarToTypeName
                                     || settings.bindStarToLeftSpecifier
                                     || settings.bindStarToRightSpecifier;

    if ((settings.bindStarToIdentifier || settings.bindStarToRightSpecifier)
        && ClangFormatSettings::instance().mode() == ClangFormatSettings::Mode::Formatting)
        m_style.PointerAlignment = FormatStyle::PAS_Right;

    if ((settings.bindStarToTypeName || settings.bindStarToLeftSpecifier)
        && ClangFormatSettings::instance().mode() == ClangFormatSettings::Mode::Formatting)
        m_style.PointerAlignment = FormatStyle::PAS_Left;

    saveNewFormat();
}

TextEditor::TabSettings ClangFormatFile::toTabSettings(ProjectExplorer::Project *project) const
{
    using namespace clang::format;
    auto settings = CppEditor::CppCodeStyleSettings::getProjectTabSettings(project);

    FormatStyle style;
    style.Language = clang::format::FormatStyle::LK_Cpp;
    const std::error_code error
        = parseConfiguration(m_filePath.fileContents().value_or(QByteArray()).toStdString(), &style);
    QTC_ASSERT(error.value() == static_cast<int>(ParseError::Success), return settings);

    settings.m_indentSize = style.IndentWidth;
    settings.m_tabSize = style.TabWidth;

    switch (style.UseTab) {
#if LLVM_VERSION_MAJOR >= 11
    case FormatStyle::UT_AlignWithSpaces:
#endif
    case FormatStyle::UT_ForIndentation:
    case FormatStyle::UT_ForContinuationAndIndentation:
        settings.m_tabPolicy = TextEditor::TabSettings::TabPolicy::MixedTabPolicy;
        break;
    case FormatStyle::UT_Never:
        settings.m_tabPolicy = TextEditor::TabSettings::TabPolicy::SpacesOnlyTabPolicy;
        break;
    case FormatStyle::UT_Always:
        settings.m_tabPolicy = TextEditor::TabSettings::TabPolicy::TabsOnlyTabPolicy;
        break;
    }

    return settings;
}

void ClangFormatFile::fromTabSettings(const TextEditor::TabSettings &settings)
{
    using namespace clang::format;

    m_style.IndentWidth = settings.m_indentSize;
    m_style.TabWidth = settings.m_tabSize;

    switch (settings.m_tabPolicy) {
    case TextEditor::TabSettings::TabPolicy::MixedTabPolicy:
        m_style.UseTab = FormatStyle::UT_ForContinuationAndIndentation;
        break;
    case TextEditor::TabSettings::TabPolicy::SpacesOnlyTabPolicy:
        m_style.UseTab = FormatStyle::UT_Never;
        break;
    case TextEditor::TabSettings::TabPolicy::TabsOnlyTabPolicy:
        m_style.UseTab = FormatStyle::UT_Always;
        break;
    }

    saveNewFormat();
}
