using System.Globalization;

internal static class FGameDataValidator
{
    public static IReadOnlyDictionary<string, FGameDataEnumDefinition> Validate(
        IReadOnlyList<FGameDataTable> tables,
        FDiagnosticBag diagnostics)
    {
        IReadOnlyDictionary<string, FGameDataEnumDefinition> enumDefinitions =
            FGameDataEnumCatalog.Build(tables, diagnostics);
        ValidateTableSet(tables, diagnostics);
        foreach (FGameDataTable table in tables)
        {
            ValidateTable(table, enumDefinitions, diagnostics);
        }

        FGameDataEnumCatalog.ValidateReferences(tables, enumDefinitions, diagnostics);
        ValidateReferences(tables, diagnostics);
        ValidateCharacterRelations(tables, diagnostics);
        ValidateAuctionCurrencyRelations(tables, diagnostics);
        ValidateMonsterSpawnRelations(tables, diagnostics);
        return enumDefinitions;
    }

    private static void ValidateTableSet(IReadOnlyList<FGameDataTable> tables, FDiagnosticBag diagnostics)
    {
        foreach (IGrouping<string, FGameDataTable> duplicateGroup in tables.GroupBy(table => table.Name, StringComparer.OrdinalIgnoreCase))
        {
            if (duplicateGroup.Count() <= 1)
            {
                continue;
            }

            foreach (FGameDataTable table in duplicateGroup)
            {
                diagnostics.Add(
                    "GD0301",
                    $"Table name '{table.Name}' is declared by more than one worksheet.",
                    table.Locate(1, 2));
            }
        }
    }

    private static void ValidateTable(
        FGameDataTable table,
        IReadOnlyDictionary<string, FGameDataEnumDefinition> enumDefinitions,
        FDiagnosticBag diagnostics)
    {
        if (table.Fields.Count == 0)
        {
            diagnostics.Add("GD0302", "Table has no fields.", table.Locate(13, 2));
            return;
        }
        if (table.Rows.Count == 0)
        {
            diagnostics.Add("GD0308", "Table must contain at least one data row.", table.Locate(14, 2));
        }

        foreach (IGrouping<string, FGameDataField> duplicateGroup in table.Fields.GroupBy(field => field.Name, StringComparer.Ordinal))
        {
            if (duplicateGroup.Count() <= 1)
            {
                continue;
            }

            foreach (FGameDataField field in duplicateGroup)
            {
                diagnostics.Add("GD0303", $"Duplicate field name '{field.Name}'.", field.Location);
            }
        }

        FGameDataField[] keyFields = table.Fields.Where(field => field.IsPrimaryKey).ToArray();
        if (keyFields.Length != 1)
        {
            diagnostics.Add(
                "GD0304",
                $"Table must define exactly one primary key, but found {keyFields.Length}.",
                table.Locate(4, 2));
        }
        else
        {
            FGameDataField key = keyFields[0];
            if (!key.Required)
            {
                diagnostics.Add("GD0305", "Primary key must be required.", key.Location);
            }
            if (key.Scope != EGameDataScope.Shared)
            {
                diagnostics.Add("GD0306", "Primary key scope must be Shared.", key.Location);
            }
            if (key.Type?.Kind is EGameDataScalarKind.Float or EGameDataScalarKind.Double or EGameDataScalarKind.Bool)
            {
                diagnostics.Add("GD0307", $"Type '{key.Type.SourceName}' cannot be used as a primary key.", key.Location);
            }
        }

        ValidateScopes(table, diagnostics);
        ValidateFieldDefinitions(table, diagnostics);
        ValidateDuplicateKeys(table, diagnostics);

        if (string.Equals(table.Name, "Item", StringComparison.OrdinalIgnoreCase))
        {
            if (!string.Equals(table.Name, "Item", StringComparison.Ordinal))
            {
                diagnostics.Add("GD0408", "Item table name is case-sensitive and must be exactly 'Item'.", table.Locate(1, 2));
            }
            ValidateItemTable(table, enumDefinitions, diagnostics);
        }
        else if (string.Equals(table.Name, "AuctionPolicy", StringComparison.OrdinalIgnoreCase))
        {
            if (!string.Equals(table.Name, "AuctionPolicy", StringComparison.Ordinal))
            {
                diagnostics.Add("GD0505", "AuctionPolicy table name is case-sensitive and must be exactly 'AuctionPolicy'.", table.Locate(1, 2));
            }
            ValidateAuctionPolicyTable(table, diagnostics);
        }
        else if (string.Equals(table.Name, "Map", StringComparison.OrdinalIgnoreCase))
        {
            if (!string.Equals(table.Name, "Map", StringComparison.Ordinal))
            {
                diagnostics.Add("GD0601", "Map table name is case-sensitive and must be exactly 'Map'.", table.Locate(1, 2));
            }
            ValidateMapTable(table, enumDefinitions, diagnostics);
        }
        else if (string.Equals(table.Name, "Character", StringComparison.OrdinalIgnoreCase))
        {
            if (!string.Equals(table.Name, "Character", StringComparison.Ordinal))
            {
                diagnostics.Add("GD0701", "Character table name is case-sensitive and must be exactly 'Character'.", table.Locate(1, 2));
            }
            ValidateCharacterTable(table, diagnostics);
        }
        else if (string.Equals(table.Name, "CharacterLevel", StringComparison.OrdinalIgnoreCase))
        {
            if (!string.Equals(table.Name, "CharacterLevel", StringComparison.Ordinal))
            {
                diagnostics.Add("GD0801", "CharacterLevel table name is case-sensitive and must be exactly 'CharacterLevel'.", table.Locate(1, 2));
            }
            ValidateCharacterLevelTable(table, diagnostics);
        }
        else if (string.Equals(table.Name, "CombatFormulaPolicy", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "CombatFormulaPolicy", "GD1001", diagnostics);
            ValidateCombatFormulaPolicyTable(table, diagnostics);
        }
        else if (string.Equals(table.Name, "InventoryPolicy", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "InventoryPolicy", "GD1101", diagnostics);
            ValidateInventoryPolicyTable(table, diagnostics);
        }
        else if (string.Equals(table.Name, "Currency", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "Currency", "GD1201", diagnostics);
            ValidateCurrencyTable(table, diagnostics);
        }
        else if (string.Equals(table.Name, "MailPolicy", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "MailPolicy", "GD1301", diagnostics);
            ValidateMailPolicyTable(table, diagnostics);
        }
        else if (string.Equals(table.Name, "MailTemplate", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "MailTemplate", "GD1401", diagnostics);
            ValidateMailTemplateTable(table, enumDefinitions, diagnostics);
        }
        else if (string.Equals(table.Name, "StatConversion", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "StatConversion", "GD1501", diagnostics);
            ValidateStatConversionTable(table, enumDefinitions, diagnostics);
        }
        else if (string.Equals(table.Name, "Monster", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "Monster", "GD1601", diagnostics);
            ValidateMonsterTable(table, enumDefinitions, diagnostics);
        }
        else if (string.Equals(table.Name, "SpawnArea", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "SpawnArea", "GD1701", diagnostics);
            ValidateSpawnAreaTable(table, diagnostics);
        }
        else if (string.Equals(table.Name, "MonsterSpawner", StringComparison.OrdinalIgnoreCase))
        {
            ValidateCaseSensitiveTableName(table, "MonsterSpawner", "GD1801", diagnostics);
            ValidateMonsterSpawnerTable(table, diagnostics);
        }
    }

    private static void ValidateScopes(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        foreach (FGameDataField field in table.Fields)
        {
            if (table.Target == EGameDataTarget.Server && field.Scope == EGameDataScope.Client)
            {
                diagnostics.Add("GD0310", "A Server table cannot contain a Client-only field.", field.Location);
            }
            else if (table.Target == EGameDataTarget.Client && field.Scope == EGameDataScope.Server)
            {
                diagnostics.Add("GD0311", "A Client table cannot contain a Server-only field.", field.Location);
            }
        }
    }

    private static void ValidateFieldDefinitions(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        var generatedCppMembers = new HashSet<string>(StringComparer.Ordinal);
        var generatedCSharpProperties = new HashSet<string>(StringComparer.Ordinal);

        foreach (FGameDataField field in table.Fields)
        {
            string cppMemberName = FGameDataNaming.ToCppMemberName(table.Name, field.Name);
            string csharpPropertyName = FGameDataNaming.ToPascalCase(field.Name);
            if (!FGameDataNaming.IsCppIdentifier(cppMemberName))
            {
                diagnostics.Add("GD0325", $"Field '{field.Name}' produces invalid or reserved C++ member name '{cppMemberName}'.", field.Location);
            }
            if (!FGameDataNaming.IsCSharpIdentifier(csharpPropertyName))
            {
                diagnostics.Add("GD0326", $"Field '{field.Name}' produces invalid or reserved C# property name '{csharpPropertyName}'.", field.Location);
            }
            if (string.Equals(csharpPropertyName, "DataId", StringComparison.Ordinal))
            {
                diagnostics.Add("GD0327", "Generated C# property name 'DataId' conflicts with GameDataRow.DataId.", field.Location);
            }
            if (!generatedCppMembers.Add(cppMemberName))
            {
                diagnostics.Add("GD0312", $"Field '{field.Name}' collides with another generated C++ member name.", field.Location);
            }
            if (!generatedCSharpProperties.Add(csharpPropertyName))
            {
                diagnostics.Add("GD0313", $"Field '{field.Name}' collides with another generated C# property name.", field.Location);
            }

            if (!field.Required && field.DefaultText == null)
            {
                diagnostics.Add(
                    "GD0324",
                    $"Optional field '{field.Name}' must define #Default so generated C++/C# types have one consistent value contract.",
                    table.Locate(10, field.ColumnNumber));
            }

            if (field.Type == null)
            {
                continue;
            }

            if ((field.MinimumText != null || field.MaximumText != null) &&
                !field.Type.IsNumeric && field.Type.Kind != EGameDataScalarKind.String)
            {
                diagnostics.Add("GD0314", "#Min/#Max are only valid for numeric values or string lengths.", field.Location);
            }

            if (field.MinimumText != null && !double.TryParse(field.MinimumText, NumberStyles.Float, CultureInfo.InvariantCulture, out _))
            {
                diagnostics.Add("GD0315", $"Invalid #Min value '{field.MinimumText}'.", table.Locate(8, field.ColumnNumber));
            }
            if (field.MaximumText != null && !double.TryParse(field.MaximumText, NumberStyles.Float, CultureInfo.InvariantCulture, out _))
            {
                diagnostics.Add("GD0316", $"Invalid #Max value '{field.MaximumText}'.", table.Locate(9, field.ColumnNumber));
            }
            if (field.MinimumText != null && field.MaximumText != null &&
                double.TryParse(field.MinimumText, NumberStyles.Float, CultureInfo.InvariantCulture, out double minimum) &&
                double.TryParse(field.MaximumText, NumberStyles.Float, CultureInfo.InvariantCulture, out double maximum) &&
                minimum > maximum)
            {
                diagnostics.Add("GD0317", "#Min cannot be greater than #Max.", table.Locate(9, field.ColumnNumber));
            }

            if (field.DefaultText != null)
            {
                if (!FWorkbookParser.TryParseScalar(field.Type, field.DefaultText, out object? defaultValue))
                {
                    diagnostics.Add(
                        "GD0318",
                        $"Default value '{field.DefaultText}' is invalid for type '{field.Type.SourceName}'.",
                        table.Locate(10, field.ColumnNumber));
                }
                else if (defaultValue != null)
                {
                    ValidateDefaultValue(table, field, defaultValue, diagnostics);
                }
            }

            if (field.Type.Kind != EGameDataScalarKind.Enum)
            {
                foreach (SEnumValue allowedValue in field.AllowedValues)
                {
                    if (!FWorkbookParser.TryParseScalar(field.Type, allowedValue.Name, out _))
                    {
                        diagnostics.Add(
                            "GD0322",
                            $"#Allowed value '{allowedValue.Name}' is invalid for type '{field.Type.SourceName}'.",
                            table.Locate(11, field.ColumnNumber));
                    }
                    if (allowedValue.NumericValue.HasValue)
                    {
                        diagnostics.Add(
                            "GD0322",
                            "Numeric assignments in #Allowed are only valid for enum fields.",
                            table.Locate(11, field.ColumnNumber));
                    }
                }
            }

            // Enum values and underlying types are defined once in the global Enums table.
            // Cross-table references and row values are validated after all tables are parsed.
        }
    }

    private static void ValidateDefaultValue(
        FGameDataTable table,
        FGameDataField field,
        object defaultValue,
        FDiagnosticBag diagnostics)
    {
        if (field.AllowedValues.Count != 0 && !IsAllowedValue(field, defaultValue))
        {
            diagnostics.Add(
                "GD0322",
                $"Default value '{field.DefaultText}' is not included in #Allowed.",
                table.Locate(10, field.ColumnNumber));
        }

        if (field.Type == null || (!field.Type.IsNumeric && field.Type.Kind != EGameDataScalarKind.String))
        {
            return;
        }

        double measuredValue = field.Type.Kind == EGameDataScalarKind.String
            ? ((string)defaultValue).Length
            : Convert.ToDouble(defaultValue, CultureInfo.InvariantCulture);
        if (field.MinimumText != null &&
            double.TryParse(field.MinimumText, NumberStyles.Float, CultureInfo.InvariantCulture, out double minimum) &&
            measuredValue < minimum)
        {
            diagnostics.Add(
                "GD0323",
                $"Default value '{field.DefaultText}' is below #Min '{field.MinimumText}'.",
                table.Locate(10, field.ColumnNumber));
        }
        if (field.MaximumText != null &&
            double.TryParse(field.MaximumText, NumberStyles.Float, CultureInfo.InvariantCulture, out double maximum) &&
            measuredValue > maximum)
        {
            diagnostics.Add(
                "GD0323",
                $"Default value '{field.DefaultText}' exceeds #Max '{field.MaximumText}'.",
                table.Locate(10, field.ColumnNumber));
        }
    }

    private static bool IsAllowedValue(FGameDataField field, object value)
    {
        if (field.Type == null)
        {
            return false;
        }

        string canonicalValue = FGameDataText.FormatScalar(value, field.Type);
        foreach (SEnumValue allowedValue in field.AllowedValues)
        {
            if (FWorkbookParser.TryParseScalar(field.Type, allowedValue.Name, out object? parsedAllowed) &&
                parsedAllowed != null &&
                string.Equals(canonicalValue, FGameDataText.FormatScalar(parsedAllowed, field.Type), StringComparison.Ordinal))
            {
                return true;
            }
        }
        return false;
    }

    private static void ValidateDuplicateKeys(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        FGameDataField? primaryKey = table.PrimaryKey;
        if (primaryKey?.Type == null)
        {
            return;
        }

        var firstRows = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (FGameDataRow row in table.Rows)
        {
            row.Values.TryGetValue(primaryKey.Name, out object? keyValue);
            if (keyValue == null)
            {
                continue;
            }

            string canonicalKey = FGameDataText.FormatScalar(keyValue, primaryKey.Type!);
            if (firstRows.TryGetValue(canonicalKey, out int firstRow))
            {
                diagnostics.Add(
                    "GD0330",
                    $"Duplicate primary key '{canonicalKey}'. The first occurrence is row {firstRow}.",
                    table.Locate(row.SourceRowNumber, primaryKey.ColumnNumber));
            }
            else
            {
                firstRows.Add(canonicalKey, row.SourceRowNumber);
            }
        }
    }

    private static void ValidateReferences(IReadOnlyList<FGameDataTable> tables, FDiagnosticBag diagnostics)
    {
        Dictionary<string, FGameDataTable> tableByName = tables
            .GroupBy(table => table.Name, StringComparer.Ordinal)
            .Where(group => group.Count() == 1)
            .ToDictionary(group => group.Key, group => group.Single(), StringComparer.Ordinal);

        foreach (FGameDataTable table in tables)
        {
            foreach (FGameDataField field in table.Fields.Where(field => !string.IsNullOrWhiteSpace(field.Reference)))
            {
                string[] parts = field.Reference!.Split('.', StringSplitOptions.TrimEntries);
                if (parts.Length != 2 || !tableByName.TryGetValue(parts[0], out FGameDataTable? referencedTable))
                {
                    diagnostics.Add("GD0340", $"Reference '{field.Reference}' must identify an existing Table.Field.", table.Locate(12, field.ColumnNumber));
                    continue;
                }

                FGameDataField? referencedField = referencedTable.Fields.FirstOrDefault(candidate => candidate.Name == parts[1]);
                if (referencedField == null)
                {
                    diagnostics.Add("GD0341", $"Referenced field '{field.Reference}' does not exist.", table.Locate(12, field.ColumnNumber));
                    continue;
                }
                if (!referencedField.IsPrimaryKey)
                {
                    diagnostics.Add("GD0342", $"Referenced field '{field.Reference}' must be the target table's primary key.", table.Locate(12, field.ColumnNumber));
                }
                if (!AreEquivalentTypes(field.Type, referencedField.Type))
                {
                    diagnostics.Add("GD0343", $"Reference type mismatch: '{field.Type?.SourceName}' -> '{referencedField.Type?.SourceName}'.", table.Locate(12, field.ColumnNumber));
                    continue;
                }
                if (field.Type == null || referencedField.Type == null)
                {
                    continue;
                }

                var referencedValues = referencedTable.Rows
                    .Select(row => row.Values.TryGetValue(referencedField.Name, out object? value) ? value : null)
                    .Where(value => value != null)
                    .Select(value => FGameDataText.FormatScalar(value!, referencedField.Type!))
                    .ToHashSet(StringComparer.Ordinal);
                foreach (FGameDataRow row in table.Rows)
                {
                    row.Values.TryGetValue(field.Name, out object? value);
                    if (value == null)
                    {
                        continue;
                    }
                    string canonical = FGameDataText.FormatScalar(value, field.Type!);
                    if (!referencedValues.Contains(canonical))
                    {
                        diagnostics.Add("GD0344", $"Value '{canonical}' does not exist in referenced field '{field.Reference}'.", table.Locate(row.SourceRowNumber, field.ColumnNumber));
                    }
                }
            }
        }
    }

    private static void ValidateItemTable(
        FGameDataTable table,
        IReadOnlyDictionary<string, FGameDataEnumDefinition> enumDefinitions,
        FDiagnosticBag diagnostics)
    {
        string[] requiredFields =
        [
            "ItemDataId",
            "Name",
            "Category",
            "EquipmentSlot",
            "MaxStack",
            "Tradable",
            "Attack",
            "Str",
            "Dex",
            "Int",
            "Luk",
        ];
        Dictionary<string, FGameDataField> fields = table.Fields
            .GroupBy(field => field.Name, StringComparer.Ordinal)
            .ToDictionary(group => group.Key, group => group.First(), StringComparer.Ordinal);
        FGameDataField[] unsupportedAdditionalFields = table.Fields
            .Where(field =>
                !requiredFields.Contains(field.Name, StringComparer.Ordinal) &&
                field.Scope is EGameDataScope.Shared or EGameDataScope.Server)
            .ToArray();
        if (unsupportedAdditionalFields.Length != 0)
        {
            diagnostics.Add(
                "GD0409",
                $"Item C++ loader does not support additional Shared/Server fields: {string.Join(", ", unsupportedAdditionalFields.Select(field => field.Name))}.",
                unsupportedAdditionalFields[0].Location);
        }
        foreach (string requiredField in requiredFields)
        {
            if (!fields.ContainsKey(requiredField))
            {
                diagnostics.Add("GD0401", $"Item table requires field '{requiredField}'.", table.Locate(13, 2));
            }
        }
        if (requiredFields.Any(field => !fields.ContainsKey(field)))
        {
            return;
        }

        if (table.Target != EGameDataTarget.Shared)
        {
            diagnostics.Add("GD0407", "Item table #Target must be Shared.", table.Locate(2, 2));
        }

        ValidateItemField(table, fields["ItemDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, diagnostics, expectedEnumName: null, mustBePrimaryKey: true);
        ValidateItemField(table, fields["Name"], EGameDataScalarKind.String, EGameDataScope.Shared, diagnostics);
        ValidateItemField(table, fields["Category"], EGameDataScalarKind.Enum, EGameDataScope.Shared, diagnostics, "ItemCategory");
        ValidateItemField(table, fields["EquipmentSlot"], EGameDataScalarKind.Enum, EGameDataScope.Shared, diagnostics, "EquipmentSlot");
        ValidateItemField(table, fields["MaxStack"], EGameDataScalarKind.UInt32, EGameDataScope.Server, diagnostics);
        ValidateItemField(table, fields["Tradable"], EGameDataScalarKind.Bool, EGameDataScope.Server, diagnostics);
        ValidateItemField(table, fields["Attack"], EGameDataScalarKind.UInt32, EGameDataScope.Server, diagnostics);
        ValidateItemField(table, fields["Str"], EGameDataScalarKind.UInt32, EGameDataScope.Server, diagnostics);
        ValidateItemField(table, fields["Dex"], EGameDataScalarKind.UInt32, EGameDataScope.Server, diagnostics);
        ValidateItemField(table, fields["Int"], EGameDataScalarKind.UInt32, EGameDataScope.Server, diagnostics);
        ValidateItemField(table, fields["Luk"], EGameDataScalarKind.UInt32, EGameDataScope.Server, diagnostics);

        SEnumValue[] expectedCategories =
        [
            new("Equipment", 1),
            new("Consumable", 2),
            new("Material", 3),
        ];
        if (enumDefinitions.TryGetValue("ItemCategory", out FGameDataEnumDefinition? itemCategory) &&
            (itemCategory.Target != EGameDataTarget.Shared ||
             itemCategory.UnderlyingType != EGameDataEnumUnderlyingType.UInt8 ||
             !itemCategory.Values.SequenceEqual(expectedCategories)))
        {
            diagnostics.Add(
                "GD0414",
                "Shared uint8 enum 'ItemCategory' must define Equipment=1|Consumable=2|Material=3.",
                itemCategory.Location);
        }
        SEnumValue[] expectedEquipmentSlots =
        [
            new("None", 0),
            new("Weapon", 1),
            new("Armor", 2),
            new("Accessory", 3),
        ];
        if (enumDefinitions.TryGetValue("EquipmentSlot", out FGameDataEnumDefinition? equipmentSlot) &&
            (equipmentSlot.Target != EGameDataTarget.Shared ||
             equipmentSlot.UnderlyingType != EGameDataEnumUnderlyingType.UInt8 ||
             !equipmentSlot.Values.SequenceEqual(expectedEquipmentSlots)))
        {
            diagnostics.Add(
                "GD0416",
                "Shared uint8 enum 'EquipmentSlot' must define None=0|Weapon=1|Armor=2|Accessory=3.",
                equipmentSlot.Location);
        }
        if (!string.Equals(fields["Name"].MaximumText, "100", StringComparison.Ordinal))
        {
            diagnostics.Add(
                "GD0415",
                "Item.Name #Max must be 100 characters.",
                table.Locate(9, fields["Name"].ColumnNumber));
        }

        foreach (FGameDataRow row in table.Rows)
        {
            uint itemDataId = GetUInt32(row, "ItemDataId");
            string name = GetString(row, "Name");
            string category = GetString(row, "Category");
            string equipmentSlotName = GetString(row, "EquipmentSlot");
            uint maxStack = GetUInt32(row, "MaxStack");
            uint attack = GetUInt32(row, "Attack");
            uint str = GetUInt32(row, "Str");
            uint dex = GetUInt32(row, "Dex");
            uint intelligence = GetUInt32(row, "Int");
            uint luk = GetUInt32(row, "Luk");

            if (itemDataId == 0)
            {
                diagnostics.Add("GD0402", "ItemDataId must be greater than zero.", table.Locate(row.SourceRowNumber, fields["ItemDataId"].ColumnNumber));
            }
            if (string.IsNullOrWhiteSpace(name))
            {
                diagnostics.Add("GD0403", "Item Name must not be empty.", table.Locate(row.SourceRowNumber, fields["Name"].ColumnNumber));
            }
            if (maxStack == 0)
            {
                diagnostics.Add("GD0404", "MaxStack must be greater than zero.", table.Locate(row.SourceRowNumber, fields["MaxStack"].ColumnNumber));
            }
            if (category == "Equipment" && maxStack != 1)
            {
                diagnostics.Add("GD0405", "Equipment items must have MaxStack=1.", table.Locate(row.SourceRowNumber, fields["MaxStack"].ColumnNumber));
            }
            if (category == "Equipment" && equipmentSlotName == "None")
            {
                diagnostics.Add("GD0417", "Equipment items must use a non-None EquipmentSlot.", table.Locate(row.SourceRowNumber, fields["EquipmentSlot"].ColumnNumber));
            }
            if (category != "Equipment" && equipmentSlotName != "None")
            {
                diagnostics.Add("GD0417", "Non-equipment items must use EquipmentSlot=None.", table.Locate(row.SourceRowNumber, fields["EquipmentSlot"].ColumnNumber));
            }
            if (category != "Equipment" && (attack != 0 || str != 0 || dex != 0 || intelligence != 0 || luk != 0))
            {
                diagnostics.Add("GD0406", "Non-equipment items must have Attack/STR/DEX/INT/LUK all set to zero.", table.Locate(row.SourceRowNumber, fields["Category"].ColumnNumber));
            }
        }
    }

    private static void ValidateItemField(
        FGameDataTable table,
        FGameDataField field,
        EGameDataScalarKind expectedKind,
        EGameDataScope expectedScope,
        FDiagnosticBag diagnostics,
        string? expectedEnumName = null,
        bool mustBePrimaryKey = false)
    {
        if (field.Type?.Kind != expectedKind ||
            (expectedEnumName != null && field.Type.EnumName != expectedEnumName))
        {
            string expectedType = expectedEnumName == null
                ? expectedKind.ToString().ToLowerInvariant()
                : $"enum<{expectedEnumName}>";
            diagnostics.Add("GD0410", $"Item.{field.Name} must use type '{expectedType}'.", table.Locate(6, field.ColumnNumber));
        }
        if (field.Scope != expectedScope)
        {
            diagnostics.Add("GD0411", $"Item.{field.Name} must use scope '{expectedScope}'.", table.Locate(5, field.ColumnNumber));
        }
        if (!field.Required)
        {
            diagnostics.Add("GD0412", $"Item.{field.Name} must be required.", table.Locate(7, field.ColumnNumber));
        }
        if (mustBePrimaryKey && !field.IsPrimaryKey)
        {
            diagnostics.Add("GD0413", $"Item.{field.Name} must be the primary key.", table.Locate(4, field.ColumnNumber));
        }
    }

    private static void ValidateAuctionPolicyTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields =
        [
            "AuctionPolicyId",
            "MaxActiveListings",
            "SearchPageSize",
            "MinimumListingDurationSeconds",
            "MaximumListingDurationSeconds",
            "DefaultListingDurationSeconds",
            "DefaultCurrencyDataId",
            "MinimumBidIncrement",
            "MinimumListingPrice",
            "MaximumListingPrice",
        ];
        Dictionary<string, FGameDataField> fields = table.Fields
            .GroupBy(field => field.Name, StringComparer.Ordinal)
            .ToDictionary(group => group.Key, group => group.First(), StringComparer.Ordinal);

        FGameDataField[] unsupportedFields = table.Fields
            .Where(field => !requiredFields.Contains(field.Name, StringComparer.Ordinal))
            .ToArray();
        if (unsupportedFields.Length != 0)
        {
            diagnostics.Add(
                "GD0502",
                $"AuctionPolicy loader does not support additional fields: {string.Join(", ", unsupportedFields.Select(field => field.Name))}.",
                unsupportedFields[0].Location);
        }

        foreach (string requiredField in requiredFields)
        {
            if (!fields.ContainsKey(requiredField))
            {
                diagnostics.Add("GD0501", $"AuctionPolicy table requires field '{requiredField}'.", table.Locate(13, 2));
            }
        }
        if (requiredFields.Any(field => !fields.ContainsKey(field)))
        {
            return;
        }

        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD0503", "AuctionPolicy table #Target must be Server.", table.Locate(2, 2));
        }

        ValidateAuctionPolicyField(table, fields["AuctionPolicyId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, diagnostics, mustBePrimaryKey: true);
        foreach (string fieldName in requiredFields.Skip(1).Take(6))
        {
            ValidateAuctionPolicyField(table, fields[fieldName], EGameDataScalarKind.UInt32, EGameDataScope.Server, diagnostics);
        }
        ValidateAuctionPolicyField(table, fields["MinimumBidIncrement"], EGameDataScalarKind.UInt64, EGameDataScope.Server, diagnostics);
        ValidateAuctionPolicyField(table, fields["MinimumListingPrice"], EGameDataScalarKind.UInt64, EGameDataScope.Server, diagnostics);
        ValidateAuctionPolicyField(table, fields["MaximumListingPrice"], EGameDataScalarKind.UInt64, EGameDataScope.Server, diagnostics);

        if (table.Rows.Count != 1)
        {
            diagnostics.Add("GD0506", "AuctionPolicy table must contain exactly one policy row.", table.Locate(14, 2));
        }

        foreach (FGameDataRow row in table.Rows)
        {
            uint auctionPolicyId = GetUInt32(row, "AuctionPolicyId");
            uint maxActiveListings = GetUInt32(row, "MaxActiveListings");
            uint searchPageSize = GetUInt32(row, "SearchPageSize");
            uint minimumDuration = GetUInt32(row, "MinimumListingDurationSeconds");
            uint maximumDuration = GetUInt32(row, "MaximumListingDurationSeconds");
            uint defaultDuration = GetUInt32(row, "DefaultListingDurationSeconds");
            uint defaultCurrencyDataId = GetUInt32(row, "DefaultCurrencyDataId");
            ulong minimumBidIncrement = GetUInt64(row, "MinimumBidIncrement");
            ulong minimumListingPrice = GetUInt64(row, "MinimumListingPrice");
            ulong maximumListingPrice = GetUInt64(row, "MaximumListingPrice");

            if (auctionPolicyId != 1)
            {
                diagnostics.Add("GD0507", "AuctionPolicyId must be 1.", table.Locate(row.SourceRowNumber, fields["AuctionPolicyId"].ColumnNumber));
            }
            if (maxActiveListings is 0 or >= 100)
            {
                diagnostics.Add("GD0508", "MaxActiveListings must be between 1 and 99.", table.Locate(row.SourceRowNumber, fields["MaxActiveListings"].ColumnNumber));
            }
            if (searchPageSize is 0 or >= 100)
            {
                diagnostics.Add("GD0508", "SearchPageSize must be between 1 and 99.", table.Locate(row.SourceRowNumber, fields["SearchPageSize"].ColumnNumber));
            }
            if (minimumDuration == 0 || minimumDuration > maximumDuration ||
                defaultDuration < minimumDuration || defaultDuration > maximumDuration)
            {
                diagnostics.Add(
                    "GD0508",
                    "Listing durations must satisfy 0 < MinimumListingDurationSeconds <= DefaultListingDurationSeconds <= MaximumListingDurationSeconds.",
                    table.Locate(row.SourceRowNumber, fields["DefaultListingDurationSeconds"].ColumnNumber));
            }
            if (defaultCurrencyDataId == 0 || minimumBidIncrement == 0 || minimumListingPrice == 0 ||
                minimumListingPrice > maximumListingPrice || minimumBidIncrement > maximumListingPrice)
            {
                diagnostics.Add(
                    "GD0509",
                    "Auction price policy must satisfy non-zero DefaultCurrencyDataId and 0 < MinimumBidIncrement, MinimumListingPrice <= MaximumListingPrice.",
                    table.Locate(row.SourceRowNumber, fields["MaximumListingPrice"].ColumnNumber));
            }
        }
    }

    private static void ValidateAuctionPolicyField(
        FGameDataTable table,
        FGameDataField field,
        EGameDataScalarKind expectedKind,
        EGameDataScope expectedScope,
        FDiagnosticBag diagnostics,
        bool mustBePrimaryKey = false)
    {
        if (field.Type?.Kind != expectedKind)
        {
            diagnostics.Add("GD0504", $"AuctionPolicy.{field.Name} must use type '{expectedKind.ToString().ToLowerInvariant()}'.", table.Locate(6, field.ColumnNumber));
        }
        if (field.Scope != expectedScope)
        {
            diagnostics.Add("GD0504", $"AuctionPolicy.{field.Name} must use scope '{expectedScope}'.", table.Locate(5, field.ColumnNumber));
        }
        if (!field.Required)
        {
            diagnostics.Add("GD0504", $"AuctionPolicy.{field.Name} must be required.", table.Locate(7, field.ColumnNumber));
        }
        if (mustBePrimaryKey && !field.IsPrimaryKey)
        {
            diagnostics.Add("GD0504", $"AuctionPolicy.{field.Name} must be the primary key.", table.Locate(4, field.ColumnNumber));
        }
    }

    private static void ValidateMapTable(
        FGameDataTable table,
        IReadOnlyDictionary<string, FGameDataEnumDefinition> enumDefinitions,
        FDiagnosticBag diagnostics)
    {
        string[] requiredFields =
        [
            "MapDataId",
            "Name",
            "MapType",
            "WorldWidth",
            "WorldHeight",
            "SectorSize",
            "AoiSectorRadius",
            "SpawnX",
            "SpawnY",
            "SectorExecutionMode",
            "MapAssetKey",
        ];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD0602", diagnostics))
        {
            return;
        }

        if (table.Target != EGameDataTarget.Shared)
        {
            diagnostics.Add("GD0603", "Map table #Target must be Shared.", table.Locate(2, 2));
        }

        ValidateContractField(table, fields["MapDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD0604", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["Name"], EGameDataScalarKind.String, EGameDataScope.Shared, "GD0604", diagnostics);
        ValidateEnumContractField(table, fields["MapType"], "MapType", EGameDataScope.Shared, "GD0604", diagnostics);
        ValidateContractField(table, fields["WorldWidth"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD0604", diagnostics);
        ValidateContractField(table, fields["WorldHeight"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD0604", diagnostics);
        ValidateContractField(table, fields["SectorSize"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD0604", diagnostics);
        ValidateContractField(table, fields["AoiSectorRadius"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD0604", diagnostics);
        ValidateContractField(table, fields["SpawnX"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD0604", diagnostics);
        ValidateContractField(table, fields["SpawnY"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD0604", diagnostics);
        ValidateEnumContractField(
            table,
            fields["SectorExecutionMode"],
            "SectorExecutionMode",
            EGameDataScope.Server,
            "GD0604",
            diagnostics);
        ValidateContractField(table, fields["MapAssetKey"], EGameDataScalarKind.String, EGameDataScope.Client, "GD0604", diagnostics);

        SEnumValue[] expectedMapTypes = [new("Town", 1), new("Dungeon", 2)];
        if (enumDefinitions.TryGetValue("MapType", out FGameDataEnumDefinition? mapType) &&
            (mapType.Target != EGameDataTarget.Shared ||
             mapType.UnderlyingType != EGameDataEnumUnderlyingType.UInt8 ||
             !mapType.Values.SequenceEqual(expectedMapTypes)))
        {
            diagnostics.Add(
                "GD0608",
                "Shared uint8 enum 'MapType' must define Town=1|Dungeon=2.",
                mapType.Location);
        }

        SEnumValue[] expectedSectorExecutionModes = [new("Serial", 1), new("TaskGraph", 2)];
        if (enumDefinitions.TryGetValue("SectorExecutionMode", out FGameDataEnumDefinition? sectorExecutionMode) &&
            (sectorExecutionMode.Target != EGameDataTarget.Server ||
             sectorExecutionMode.UnderlyingType != EGameDataEnumUnderlyingType.UInt8 ||
             !sectorExecutionMode.Values.SequenceEqual(expectedSectorExecutionModes)))
        {
            diagnostics.Add(
                "GD0609",
                "Server uint8 enum 'SectorExecutionMode' must define Serial=1|TaskGraph=2.",
                sectorExecutionMode.Location);
        }

        foreach (FGameDataRow row in table.Rows)
        {
            uint worldWidth = GetUInt32(row, "WorldWidth");
            uint worldHeight = GetUInt32(row, "WorldHeight");
            uint sectorSize = GetUInt32(row, "SectorSize");
            float spawnX = GetFloat(row, "SpawnX");
            float spawnY = GetFloat(row, "SpawnY");

            if (sectorSize == 0 || worldWidth == 0 || worldHeight == 0 ||
                worldWidth % sectorSize != 0 || worldHeight % sectorSize != 0)
            {
                diagnostics.Add(
                    "GD0605",
                    "WorldWidth and WorldHeight must be positive multiples of SectorSize.",
                    table.Locate(row.SourceRowNumber, fields["SectorSize"].ColumnNumber));
            }
            if (spawnX < 0.0f || spawnX >= worldWidth)
            {
                diagnostics.Add(
                    "GD0606",
                    "SpawnX must be inside the world boundary [0, WorldWidth).",
                    table.Locate(row.SourceRowNumber, fields["SpawnX"].ColumnNumber));
            }
            if (spawnY < 0.0f || spawnY >= worldHeight)
            {
                diagnostics.Add(
                    "GD0607",
                    "SpawnY must be inside the world boundary [0, WorldHeight).",
                    table.Locate(row.SourceRowNumber, fields["SpawnY"].ColumnNumber));
            }
        }
    }

    private static void ValidateEnumContractField(
        FGameDataTable table,
        FGameDataField field,
        string expectedEnumName,
        EGameDataScope expectedScope,
        string diagnosticCode,
        FDiagnosticBag diagnostics)
    {
        if (field.Type?.Kind != EGameDataScalarKind.Enum ||
            !string.Equals(field.Type.EnumName, expectedEnumName, StringComparison.Ordinal))
        {
            diagnostics.Add(diagnosticCode, $"{table.Name}.{field.Name} must use type 'enum<{expectedEnumName}>'.", table.Locate(6, field.ColumnNumber));
        }
        if (field.Scope != expectedScope)
        {
            diagnostics.Add(diagnosticCode, $"{table.Name}.{field.Name} must use scope '{expectedScope}'.", table.Locate(5, field.ColumnNumber));
        }
        if (!field.Required)
        {
            diagnostics.Add(diagnosticCode, $"{table.Name}.{field.Name} must be required.", table.Locate(7, field.ColumnNumber));
        }
    }

    private static void ValidateMonsterTable(
        FGameDataTable table,
        IReadOnlyDictionary<string, FGameDataEnumDefinition> enumDefinitions,
        FDiagnosticBag diagnostics)
    {
        string[] requiredFields =
        [
            "MonsterDataId",
            "Name",
            "MonsterType",
            "AggroType",
            "MaxHp",
            "Attack",
            "Defense",
            "MoveSpeed",
            "CollisionRadius",
            "AggroRadius",
            "LeashRadius",
            "AttackRange",
            "AttackCooldownMilliseconds",
            "SpriteAssetKey",
        ];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1602", diagnostics))
        {
            return;
        }

        if (table.Target != EGameDataTarget.Shared)
        {
            diagnostics.Add("GD1603", "Monster table #Target must be Shared.", table.Locate(2, 2));
        }

        ValidateContractField(table, fields["MonsterDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1604", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["Name"], EGameDataScalarKind.String, EGameDataScope.Shared, "GD1604", diagnostics);
        ValidateEnumContractField(table, fields["MonsterType"], "MonsterType", EGameDataScope.Shared, "GD1604", diagnostics);
        ValidateEnumContractField(table, fields["AggroType"], "MonsterAggroType", EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["MaxHp"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["Attack"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["Defense"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["MoveSpeed"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["CollisionRadius"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["AggroRadius"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["LeashRadius"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["AttackRange"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["AttackCooldownMilliseconds"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1604", diagnostics);
        ValidateContractField(table, fields["SpriteAssetKey"], EGameDataScalarKind.String, EGameDataScope.Client, "GD1604", diagnostics);

        ValidateEnumDefinition(
            enumDefinitions,
            "MonsterType",
            EGameDataTarget.Shared,
            [new("Normal", 1), new("Boss", 2)],
            "GD1605",
            diagnostics);
        ValidateEnumDefinition(
            enumDefinitions,
            "MonsterAggroType",
            EGameDataTarget.Server,
            [new("Aggressive", 1), new("Passive", 2)],
            "GD1605",
            diagnostics);

        foreach (FGameDataRow row in table.Rows)
        {
            uint monsterDataId = GetUInt32(row, "MonsterDataId");
            string name = GetString(row, "Name");
            uint maxHp = GetUInt32(row, "MaxHp");
            uint attack = GetUInt32(row, "Attack");
            float moveSpeed = GetFloat(row, "MoveSpeed");
            float collisionRadius = GetFloat(row, "CollisionRadius");
            float aggroRadius = GetFloat(row, "AggroRadius");
            float leashRadius = GetFloat(row, "LeashRadius");
            float attackRange = GetFloat(row, "AttackRange");
            uint attackCooldownMilliseconds = GetUInt32(row, "AttackCooldownMilliseconds");
            string spriteAssetKey = GetString(row, "SpriteAssetKey");

            if (monsterDataId == 0 || string.IsNullOrWhiteSpace(name) || maxHp == 0 || attack == 0 ||
                moveSpeed <= 0.0f || collisionRadius <= 0.0f || aggroRadius <= 0.0f || leashRadius <= 0.0f || attackRange <= 0.0f ||
                attackCooldownMilliseconds == 0 || string.IsNullOrWhiteSpace(spriteAssetKey))
            {
                diagnostics.Add(
                    "GD1606",
                    "Monster requires non-zero ID/HP/Attack/Cooldown, positive movement and range values, and non-empty display/resource names.",
                    table.Locate(row.SourceRowNumber, 2));
            }

            if (attackRange > aggroRadius)
            {
                diagnostics.Add(
                    "GD1607",
                    "Monster AttackRange cannot exceed AggroRadius.",
                    table.Locate(row.SourceRowNumber, fields["AttackRange"].ColumnNumber));
            }

            if (aggroRadius > leashRadius)
            {
                diagnostics.Add(
                    "GD1608",
                    "Monster AggroRadius cannot exceed LeashRadius.",
                    table.Locate(row.SourceRowNumber, fields["LeashRadius"].ColumnNumber));
            }
        }
    }

    private static void ValidateSpawnAreaTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields = ["SpawnAreaDataId", "MapDataId", "MinX", "MinY", "MaxX", "MaxY"];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1702", diagnostics))
        {
            return;
        }

        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD1703", "SpawnArea table #Target must be Server.", table.Locate(2, 2));
        }

        ValidateContractField(table, fields["SpawnAreaDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1704", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["MapDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1704", diagnostics);
        ValidateRequiredReference(table, fields["MapDataId"], "Map.MapDataId", "GD1707", diagnostics);
        ValidateContractField(table, fields["MinX"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1704", diagnostics);
        ValidateContractField(table, fields["MinY"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1704", diagnostics);
        ValidateContractField(table, fields["MaxX"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1704", diagnostics);
        ValidateContractField(table, fields["MaxY"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1704", diagnostics);

        foreach (FGameDataRow row in table.Rows)
        {
            float minX = GetFloat(row, "MinX");
            float minY = GetFloat(row, "MinY");
            float maxX = GetFloat(row, "MaxX");
            float maxY = GetFloat(row, "MaxY");
            if (GetUInt32(row, "SpawnAreaDataId") == 0 || GetUInt32(row, "MapDataId") == 0 ||
                minX < 0.0f || minY < 0.0f || maxX <= minX || maxY <= minY)
            {
                diagnostics.Add(
                    "GD1705",
                    "SpawnArea requires non-zero IDs and coordinates satisfying 0 <= Min < Max.",
                    table.Locate(row.SourceRowNumber, 2));
            }
        }
    }

    private static void ValidateMonsterSpawnerTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields =
        [
            "SpawnerDataId",
            "MapDataId",
            "MonsterDataId",
            "SpawnAreaDataId",
            "InitialSpawnCount",
            "MaxAliveCount",
            "RespawnIntervalMilliseconds",
        ];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1802", diagnostics))
        {
            return;
        }

        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD1803", "MonsterSpawner table #Target must be Server.", table.Locate(2, 2));
        }

        ValidateContractField(table, fields["SpawnerDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1804", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["MapDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1804", diagnostics);
        ValidateContractField(table, fields["MonsterDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1804", diagnostics);
        ValidateContractField(table, fields["SpawnAreaDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1804", diagnostics);
        ValidateRequiredReference(table, fields["MapDataId"], "Map.MapDataId", "GD1807", diagnostics);
        ValidateRequiredReference(table, fields["MonsterDataId"], "Monster.MonsterDataId", "GD1807", diagnostics);
        ValidateRequiredReference(table, fields["SpawnAreaDataId"], "SpawnArea.SpawnAreaDataId", "GD1807", diagnostics);
        ValidateContractField(table, fields["InitialSpawnCount"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1804", diagnostics);
        ValidateContractField(table, fields["MaxAliveCount"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1804", diagnostics);
        ValidateContractField(table, fields["RespawnIntervalMilliseconds"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1804", diagnostics);

        foreach (FGameDataRow row in table.Rows)
        {
            uint initialSpawnCount = GetUInt32(row, "InitialSpawnCount");
            uint maxAliveCount = GetUInt32(row, "MaxAliveCount");
            if (GetUInt32(row, "SpawnerDataId") == 0 || GetUInt32(row, "MapDataId") == 0 ||
                GetUInt32(row, "MonsterDataId") == 0 || GetUInt32(row, "SpawnAreaDataId") == 0 ||
                initialSpawnCount == 0 || maxAliveCount == 0 || initialSpawnCount > maxAliveCount ||
                GetUInt32(row, "RespawnIntervalMilliseconds") == 0)
            {
                diagnostics.Add(
                    "GD1805",
                    "MonsterSpawner requires non-zero IDs/counts/interval and InitialSpawnCount <= MaxAliveCount.",
                    table.Locate(row.SourceRowNumber, 2));
            }
        }
    }

    private static void ValidateCharacterTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields =
        [
            "CharacterDataId",
            "Name",
            "InitialLevel",
            "InitialStr",
            "InitialDex",
            "InitialInt",
            "InitialLuk",
            "InitialUnspentStatPoints",
            "MoveSpeed",
            "CollisionRadius",
            "SpriteAssetKey",
        ];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD0702", diagnostics))
        {
            return;
        }

        if (table.Target != EGameDataTarget.Shared)
        {
            diagnostics.Add("GD0703", "Character table #Target must be Shared.", table.Locate(2, 2));
        }

        ValidateContractField(table, fields["CharacterDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD0704", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["Name"], EGameDataScalarKind.String, EGameDataScope.Shared, "GD0704", diagnostics);
        ValidateContractField(table, fields["InitialLevel"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0704", diagnostics);
        ValidateContractField(table, fields["InitialStr"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0704", diagnostics);
        ValidateContractField(table, fields["InitialDex"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0704", diagnostics);
        ValidateContractField(table, fields["InitialInt"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0704", diagnostics);
        ValidateContractField(table, fields["InitialLuk"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0704", diagnostics);
        ValidateContractField(
            table,
            fields["InitialUnspentStatPoints"],
            EGameDataScalarKind.UInt32,
            EGameDataScope.Server,
            "GD0704",
            diagnostics);
        ValidateContractField(table, fields["MoveSpeed"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD0704", diagnostics);
        ValidateContractField(table, fields["CollisionRadius"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD0704", diagnostics);
        ValidateContractField(table, fields["SpriteAssetKey"], EGameDataScalarKind.String, EGameDataScope.Client, "GD0704", diagnostics);

        foreach (FGameDataRow row in table.Rows)
        {
            if (GetFloat(row, "MoveSpeed") <= 0.0f)
            {
                diagnostics.Add("GD0705", "MoveSpeed must be greater than zero.", table.Locate(row.SourceRowNumber, fields["MoveSpeed"].ColumnNumber));
            }
            if (GetFloat(row, "CollisionRadius") <= 0.0f)
            {
                diagnostics.Add("GD0706", "CollisionRadius must be greater than zero.", table.Locate(row.SourceRowNumber, fields["CollisionRadius"].ColumnNumber));
            }
        }
    }

    private static void ValidateCharacterLevelTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields =
        [
            "CharacterLevelDataId",
            "CharacterDataId",
            "Level",
            "RequiredExpToNextLevel",
            "MaxHp",
            "MaxMp",
            "Attack",
            "Defense",
            "StatPointReward",
        ];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD0802", diagnostics))
        {
            return;
        }

        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD0803", "CharacterLevel table #Target must be Server.", table.Locate(2, 2));
        }

        ValidateContractField(table, fields["CharacterLevelDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD0804", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["CharacterDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0804", diagnostics);
        ValidateContractField(table, fields["Level"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0804", diagnostics);
        ValidateContractField(table, fields["RequiredExpToNextLevel"], EGameDataScalarKind.UInt64, EGameDataScope.Server, "GD0804", diagnostics);
        ValidateContractField(table, fields["MaxHp"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0804", diagnostics);
        ValidateContractField(table, fields["MaxMp"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0804", diagnostics);
        ValidateContractField(table, fields["Attack"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0804", diagnostics);
        ValidateContractField(table, fields["Defense"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0804", diagnostics);
        ValidateContractField(table, fields["StatPointReward"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD0804", diagnostics);
    }

    private static void ValidateCombatFormulaPolicyTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields =
        [
            "CombatFormulaPolicyId",
            "MinimumDamage",
            "PlayerBasicAttackRange",
            "PlayerBasicAttackCooldownMilliseconds",
            "PlayerRespawnDelayMilliseconds",
        ];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1002", diagnostics))
        {
            return;
        }
        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD1003", "CombatFormulaPolicy table #Target must be Server.", table.Locate(2, 2));
        }
        ValidateContractField(table, fields["CombatFormulaPolicyId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1004", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["MinimumDamage"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1004", diagnostics);
        ValidateContractField(table, fields["PlayerBasicAttackRange"], EGameDataScalarKind.Float, EGameDataScope.Server, "GD1004", diagnostics);
        ValidateContractField(table, fields["PlayerBasicAttackCooldownMilliseconds"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1004", diagnostics);
        ValidateContractField(table, fields["PlayerRespawnDelayMilliseconds"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1004", diagnostics);

        if (table.Rows.Count != 1)
        {
            diagnostics.Add("GD1005", "CombatFormulaPolicy table must contain exactly one row.", table.Locate(14, 2));
        }
        foreach (FGameDataRow row in table.Rows)
        {
            if (GetUInt32(row, "CombatFormulaPolicyId") != 1 || GetUInt32(row, "MinimumDamage") == 0 ||
                GetFloat(row, "PlayerBasicAttackRange") <= 0.0f || GetUInt32(row, "PlayerBasicAttackCooldownMilliseconds") == 0 ||
                GetUInt32(row, "PlayerRespawnDelayMilliseconds") == 0)
            {
                diagnostics.Add("GD1006", "CombatFormulaPolicy requires ID 1 and positive combat values.", table.Locate(row.SourceRowNumber, 2));
            }
        }
    }

    private static void ValidateInventoryPolicyTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields = ["InventoryPolicyId", "MaxInventorySlots", "InventoryListPageSize"];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1102", diagnostics))
        {
            return;
        }
        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD1103", "InventoryPolicy table #Target must be Server.", table.Locate(2, 2));
        }
        ValidateContractField(table, fields["InventoryPolicyId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1104", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["MaxInventorySlots"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1104", diagnostics);
        ValidateContractField(table, fields["InventoryListPageSize"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1104", diagnostics);

        if (table.Rows.Count != 1)
        {
            diagnostics.Add("GD1105", "InventoryPolicy table must contain exactly one row.", table.Locate(14, 2));
        }
        foreach (FGameDataRow row in table.Rows)
        {
            uint maxSlots = GetUInt32(row, "MaxInventorySlots");
            uint pageSize = GetUInt32(row, "InventoryListPageSize");
            if (GetUInt32(row, "InventoryPolicyId") != 1 || maxSlots == 0 || pageSize == 0 || pageSize >= 100 || pageSize > maxSlots)
            {
                diagnostics.Add("GD1106", "InventoryPolicy requires ID 1 and 0 < InventoryListPageSize < 100 and <= MaxInventorySlots.", table.Locate(row.SourceRowNumber, 2));
            }
        }
    }

    private static void ValidateCurrencyTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields = ["CurrencyDataId", "Name", "MaxAmount"];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1202", diagnostics))
        {
            return;
        }
        if (table.Target != EGameDataTarget.Shared)
        {
            diagnostics.Add("GD1203", "Currency table #Target must be Shared.", table.Locate(2, 2));
        }
        ValidateContractField(table, fields["CurrencyDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1204", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["Name"], EGameDataScalarKind.String, EGameDataScope.Shared, "GD1204", diagnostics);
        ValidateContractField(table, fields["MaxAmount"], EGameDataScalarKind.UInt64, EGameDataScope.Server, "GD1204", diagnostics);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (FGameDataRow row in table.Rows)
        {
            string name = GetString(row, "Name");
            if (GetUInt32(row, "CurrencyDataId") == 0 || GetUInt32(row, "CurrencyDataId") > ushort.MaxValue ||
                string.IsNullOrWhiteSpace(name) || GetUInt64(row, "MaxAmount") == 0)
            {
                diagnostics.Add("GD1205", "Currency requires an ID in 1..65535, non-empty Name, and positive MaxAmount.", table.Locate(row.SourceRowNumber, 2));
            }
            if (!string.IsNullOrWhiteSpace(name) && !names.Add(name))
            {
                diagnostics.Add(
                    "GD1206",
                    $"Currency Name '{name}' is duplicated (case-insensitive).",
                    table.Locate(row.SourceRowNumber, fields["Name"].ColumnNumber));
            }
        }
    }

    private static void ValidateMailPolicyTable(FGameDataTable table, FDiagnosticBag diagnostics)
    {
        string[] requiredFields = ["MailPolicyId", "MailListPageSize", "ExpirationSeconds"];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1302", diagnostics))
        {
            return;
        }
        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD1303", "MailPolicy table #Target must be Server.", table.Locate(2, 2));
        }
        ValidateContractField(table, fields["MailPolicyId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1304", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["MailListPageSize"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1304", diagnostics);
        ValidateContractField(table, fields["ExpirationSeconds"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1304", diagnostics);

        if (table.Rows.Count != 1)
        {
            diagnostics.Add("GD1305", "MailPolicy table must contain exactly one row.", table.Locate(14, 2));
        }
        foreach (FGameDataRow row in table.Rows)
        {
            if (GetUInt32(row, "MailPolicyId") != 1 || GetUInt32(row, "MailListPageSize") == 0 ||
                GetUInt32(row, "MailListPageSize") >= 100 || GetUInt32(row, "ExpirationSeconds") == 0)
            {
                diagnostics.Add("GD1306", "MailPolicy requires ID 1, a page size in 1..99, and a positive expiration.", table.Locate(row.SourceRowNumber, 2));
            }
        }
    }

    private static void ValidateMailTemplateTable(
        FGameDataTable table,
        IReadOnlyDictionary<string, FGameDataEnumDefinition> enumDefinitions,
        FDiagnosticBag diagnostics)
    {
        string[] requiredFields = ["MailTemplateDataId", "Purpose", "MailType", "Subject", "Body"];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1402", diagnostics))
        {
            return;
        }
        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD1403", "MailTemplate table #Target must be Server.", table.Locate(2, 2));
        }
        ValidateContractField(table, fields["MailTemplateDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1404", diagnostics, mustBePrimaryKey: true);
        ValidateEnumContractField(table, fields["Purpose"], "MailTemplatePurpose", EGameDataScope.Server, "GD1404", diagnostics);
        ValidateContractField(table, fields["MailType"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1404", diagnostics);
        ValidateContractField(table, fields["Subject"], EGameDataScalarKind.String, EGameDataScope.Server, "GD1404", diagnostics);
        ValidateContractField(table, fields["Body"], EGameDataScalarKind.String, EGameDataScope.Server, "GD1404", diagnostics);

        SEnumValue[] expectedPurposes =
        [
            new("AuctionPurchase", 1),
            new("AuctionSaleProceeds", 2),
            new("AuctionCancellationReturn", 3),
            new("AuctionExpirationReturn", 4),
        ];
        if (enumDefinitions.TryGetValue("MailTemplatePurpose", out FGameDataEnumDefinition? purpose) &&
            (purpose.Target != EGameDataTarget.Server || purpose.UnderlyingType != EGameDataEnumUnderlyingType.UInt8 ||
             !purpose.Values.SequenceEqual(expectedPurposes)))
        {
            diagnostics.Add("GD1405", "Server uint8 enum 'MailTemplatePurpose' has an invalid contract.", purpose.Location);
        }

        var purposes = new HashSet<string>(StringComparer.Ordinal);
        foreach (FGameDataRow row in table.Rows)
        {
            string purposeName = GetString(row, "Purpose");
            if (!purposes.Add(purposeName))
            {
                diagnostics.Add("GD1406", $"MailTemplate Purpose '{purposeName}' is duplicated.", table.Locate(row.SourceRowNumber, fields["Purpose"].ColumnNumber));
            }
            string subject = GetString(row, "Subject");
            string body = GetString(row, "Body");
            if (GetUInt32(row, "MailTemplateDataId") == 0 || GetUInt32(row, "MailType") == 0 ||
                GetUInt32(row, "MailType") > byte.MaxValue || string.IsNullOrWhiteSpace(subject) || subject.Length > 200 ||
                string.IsNullOrWhiteSpace(body) || body.Length > 2000)
            {
                diagnostics.Add("GD1407", "MailTemplate requires non-zero IDs, MailType <= 255, Subject 1..200, and Body 1..2000.", table.Locate(row.SourceRowNumber, 2));
            }
        }
        if (purposes.Count != expectedPurposes.Length)
        {
            diagnostics.Add(
                "GD1408",
                "MailTemplate must define exactly one row for every MailTemplatePurpose value.",
                table.Locate(14, fields["Purpose"].ColumnNumber));
        }
    }

    private static void ValidateStatConversionTable(
        FGameDataTable table,
        IReadOnlyDictionary<string, FGameDataEnumDefinition> enumDefinitions,
        FDiagnosticBag diagnostics)
    {
        string[] requiredFields = ["StatConversionDataId", "CharacterDataId", "SourceStat", "TargetStat", "ValuePerPointPermille"];
        Dictionary<string, FGameDataField> fields = BuildFieldMap(table);
        if (!ValidateExactFieldSet(table, fields, requiredFields, "GD1502", diagnostics))
        {
            return;
        }
        if (table.Target != EGameDataTarget.Server)
        {
            diagnostics.Add("GD1503", "StatConversion table #Target must be Server.", table.Locate(2, 2));
        }
        ValidateContractField(table, fields["StatConversionDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Shared, "GD1504", diagnostics, mustBePrimaryKey: true);
        ValidateContractField(table, fields["CharacterDataId"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1504", diagnostics);
        ValidateEnumContractField(table, fields["SourceStat"], "PrimaryStatType", EGameDataScope.Server, "GD1504", diagnostics);
        ValidateEnumContractField(table, fields["TargetStat"], "DerivedStatType", EGameDataScope.Server, "GD1504", diagnostics);
        ValidateContractField(table, fields["ValuePerPointPermille"], EGameDataScalarKind.UInt32, EGameDataScope.Server, "GD1504", diagnostics);

        SEnumValue[] expectedSources = [new("Str", 1), new("Dex", 2), new("Int", 3), new("Luk", 4)];
        SEnumValue[] expectedTargets = [new("Attack", 1), new("Defense", 2), new("MaxHp", 3), new("MaxMp", 4), new("MoveSpeed", 5)];
        ValidateEnumDefinition(enumDefinitions, "PrimaryStatType", EGameDataTarget.Server, expectedSources, "GD1505", diagnostics);
        ValidateEnumDefinition(enumDefinitions, "DerivedStatType", EGameDataTarget.Server, expectedTargets, "GD1505", diagnostics);

        var combinations = new HashSet<string>(StringComparer.Ordinal);
        foreach (FGameDataRow row in table.Rows)
        {
            string combination = $"{GetUInt32(row, "CharacterDataId")}/{GetString(row, "SourceStat")}/{GetString(row, "TargetStat")}";
            if (!combinations.Add(combination))
            {
                diagnostics.Add("GD1506", $"StatConversion combination '{combination}' is duplicated.", table.Locate(row.SourceRowNumber, fields["SourceStat"].ColumnNumber));
            }
            if (GetUInt32(row, "StatConversionDataId") == 0 || GetUInt32(row, "CharacterDataId") == 0 ||
                GetUInt32(row, "ValuePerPointPermille") == 0)
            {
                diagnostics.Add("GD1507", "StatConversion IDs and ValuePerPointPermille must be positive.", table.Locate(row.SourceRowNumber, 2));
            }
        }
    }

    private static void ValidateCharacterRelations(
        IReadOnlyList<FGameDataTable> tables,
        FDiagnosticBag diagnostics)
    {
        FGameDataTable[] characterTables = tables.Where(table => string.Equals(table.Name, "Character", StringComparison.Ordinal)).Take(2).ToArray();
        FGameDataTable[] levelTables = tables.Where(table => string.Equals(table.Name, "CharacterLevel", StringComparison.Ordinal)).Take(2).ToArray();
        if (characterTables.Length != 1 || levelTables.Length != 1)
        {
            return;
        }

        FGameDataTable characterTable = characterTables[0];
        FGameDataTable levelTable = levelTables[0];

        Dictionary<string, FGameDataField> characterFields = BuildFieldMap(characterTable);
        Dictionary<string, FGameDataField> levelFields = BuildFieldMap(levelTable);
        string[] characterRequired =
        [
            "CharacterDataId",
            "InitialLevel",
            "InitialStr",
            "InitialDex",
            "InitialInt",
            "InitialLuk",
            "InitialUnspentStatPoints",
        ];
        string[] levelRequired = ["CharacterDataId", "Level", "RequiredExpToNextLevel", "StatPointReward"];
        if (characterRequired.Any(field => !characterFields.ContainsKey(field)) ||
            levelRequired.Any(field => !levelFields.ContainsKey(field)))
        {
            return;
        }

        var rowsByCharacter = levelTable.Rows
            .GroupBy(row => GetUInt32(row, "CharacterDataId"))
            .ToDictionary(group => group.Key, group => group.OrderBy(row => GetUInt32(row, "Level")).ToArray());

        foreach ((uint characterDataId, FGameDataRow[] rows) in rowsByCharacter)
        {
            var seenLevels = new HashSet<uint>();
            for (int index = 0; index < rows.Length; ++index)
            {
                FGameDataRow row = rows[index];
                uint level = GetUInt32(row, "Level");
                if (!seenLevels.Add(level))
                {
                    diagnostics.Add(
                        "GD0805",
                        $"CharacterDataId '{characterDataId}' defines Level '{level}' more than once.",
                        levelTable.Locate(row.SourceRowNumber, levelFields["Level"].ColumnNumber));
                }

                uint expectedLevel = (uint)index + 1;
                if (level != expectedLevel)
                {
                    diagnostics.Add(
                        "GD0806",
                        $"CharacterDataId '{characterDataId}' levels must be contiguous from 1. Expected '{expectedLevel}' but found '{level}'.",
                        levelTable.Locate(row.SourceRowNumber, levelFields["Level"].ColumnNumber));
                }

                ulong requiredExp = GetUInt64(row, "RequiredExpToNextLevel");
                bool isLastLevel = index == rows.Length - 1;
                if ((!isLastLevel && requiredExp == 0) || (isLastLevel && requiredExp != 0))
                {
                    diagnostics.Add(
                        "GD0807",
                        "RequiredExpToNextLevel must be positive before the final level and zero at the final level.",
                        levelTable.Locate(row.SourceRowNumber, levelFields["RequiredExpToNextLevel"].ColumnNumber));
                }
            }
        }

        foreach (FGameDataRow characterRow in characterTable.Rows)
        {
            uint characterDataId = GetUInt32(characterRow, "CharacterDataId");
            uint initialLevel = GetUInt32(characterRow, "InitialLevel");
            if (!rowsByCharacter.TryGetValue(characterDataId, out FGameDataRow[]? levelRows))
            {
                diagnostics.Add(
                    "GD0707",
                    $"CharacterDataId '{characterDataId}' InitialLevel '{initialLevel}' has no matching CharacterLevel row.",
                    characterTable.Locate(characterRow.SourceRowNumber, characterFields["InitialLevel"].ColumnNumber));
                continue;
            }

            FGameDataRow? initialLevelRow = levelRows.FirstOrDefault(row => GetUInt32(row, "Level") == initialLevel);
            if (initialLevelRow == null)
            {
                diagnostics.Add(
                    "GD0707",
                    $"CharacterDataId '{characterDataId}' InitialLevel '{initialLevel}' has no matching CharacterLevel row.",
                    characterTable.Locate(characterRow.SourceRowNumber, characterFields["InitialLevel"].ColumnNumber));
                continue;
            }

            if (GetUInt32(initialLevelRow, "StatPointReward") != 0)
            {
                diagnostics.Add(
                    "GD0808",
                    $"CharacterDataId '{characterDataId}' InitialLevel '{initialLevel}' must have StatPointReward 0.",
                    levelTable.Locate(initialLevelRow.SourceRowNumber, levelFields["StatPointReward"].ColumnNumber));
            }

            ulong availableStatPoints = GetUInt32(characterRow, "InitialUnspentStatPoints");
            bool progressionOverflow = false;
            foreach (FGameDataRow levelRow in levelRows.Where(row => GetUInt32(row, "Level") > initialLevel))
            {
                uint reward = GetUInt32(levelRow, "StatPointReward");
                if (availableStatPoints > uint.MaxValue - (ulong)reward)
                {
                    diagnostics.Add(
                        "GD0809",
                        $"CharacterDataId '{characterDataId}' cumulative available stat points exceed uint32.",
                        levelTable.Locate(levelRow.SourceRowNumber, levelFields["StatPointReward"].ColumnNumber));
                    progressionOverflow = true;
                    break;
                }
                availableStatPoints += reward;
            }

            if (progressionOverflow)
            {
                continue;
            }

            foreach (string statField in new[] { "InitialStr", "InitialDex", "InitialInt", "InitialLuk" })
            {
                uint initialStat = GetUInt32(characterRow, statField);
                if (initialStat > uint.MaxValue - availableStatPoints)
                {
                    diagnostics.Add(
                        "GD0810",
                        $"CharacterDataId '{characterDataId}' {statField} can overflow uint32 when all obtainable points are allocated to it.",
                        characterTable.Locate(characterRow.SourceRowNumber, characterFields[statField].ColumnNumber));
                }
            }
        }
    }

    private static void ValidateAuctionCurrencyRelations(
        IReadOnlyList<FGameDataTable> tables,
        FDiagnosticBag diagnostics)
    {
        FGameDataTable? auctionPolicy = tables.SingleOrDefault(table => table.Name == "AuctionPolicy");
        FGameDataTable? currency = tables.SingleOrDefault(table => table.Name == "Currency");
        if (auctionPolicy == null || currency == null || auctionPolicy.Rows.Count != 1)
        {
            return;
        }

        Dictionary<string, FGameDataField> auctionFields = BuildFieldMap(auctionPolicy);
        Dictionary<string, FGameDataField> currencyFields = BuildFieldMap(currency);
        if (!auctionFields.ContainsKey("DefaultCurrencyDataId") || !auctionFields.ContainsKey("MaximumListingPrice") ||
            !currencyFields.ContainsKey("CurrencyDataId") || !currencyFields.ContainsKey("MaxAmount"))
        {
            return;
        }

        FGameDataRow policyRow = auctionPolicy.Rows[0];
        uint defaultCurrencyDataId = GetUInt32(policyRow, "DefaultCurrencyDataId");
        FGameDataRow? currencyRow = currency.Rows.FirstOrDefault(row => GetUInt32(row, "CurrencyDataId") == defaultCurrencyDataId);
        if (currencyRow != null && GetUInt64(policyRow, "MaximumListingPrice") > GetUInt64(currencyRow, "MaxAmount"))
        {
            diagnostics.Add(
                "GD0510",
                "AuctionPolicy.MaximumListingPrice cannot exceed the default currency MaxAmount.",
                auctionPolicy.Locate(policyRow.SourceRowNumber, auctionFields["MaximumListingPrice"].ColumnNumber));
        }
    }

    private static void ValidateMonsterSpawnRelations(
        IReadOnlyList<FGameDataTable> tables,
        FDiagnosticBag diagnostics)
    {
        FGameDataTable? mapTable = tables.FirstOrDefault(table => string.Equals(table.Name, "Map", StringComparison.Ordinal));
        FGameDataTable? monsterTable = tables.FirstOrDefault(table => string.Equals(table.Name, "Monster", StringComparison.Ordinal));
        FGameDataTable? spawnAreaTable = tables.FirstOrDefault(table => string.Equals(table.Name, "SpawnArea", StringComparison.Ordinal));
        FGameDataTable? spawnerTable = tables.FirstOrDefault(table => string.Equals(table.Name, "MonsterSpawner", StringComparison.Ordinal));
        if (mapTable == null || spawnAreaTable == null)
        {
            return;
        }

        Dictionary<string, FGameDataField> mapFields = BuildFieldMap(mapTable);
        Dictionary<string, FGameDataField> areaFields = BuildFieldMap(spawnAreaTable);
        string[] requiredMapFields = ["MapDataId", "WorldWidth", "WorldHeight"];
        string[] requiredAreaFields = ["SpawnAreaDataId", "MapDataId", "MinX", "MinY", "MaxX", "MaxY"];
        if (!requiredMapFields.All(mapFields.ContainsKey) || !requiredAreaFields.All(areaFields.ContainsKey))
        {
            return;
        }

        Dictionary<uint, FGameDataRow> mapsById = mapTable.Rows
            .Where(row => GetUInt32(row, "MapDataId") != 0)
            .GroupBy(row => GetUInt32(row, "MapDataId"))
            .ToDictionary(group => group.Key, group => group.First());
        Dictionary<uint, FGameDataRow> areasById = spawnAreaTable.Rows
            .Where(row => GetUInt32(row, "SpawnAreaDataId") != 0)
            .GroupBy(row => GetUInt32(row, "SpawnAreaDataId"))
            .ToDictionary(group => group.Key, group => group.First());

        foreach (FGameDataRow areaRow in spawnAreaTable.Rows)
        {
            uint mapDataId = GetUInt32(areaRow, "MapDataId");
            if (!mapsById.TryGetValue(mapDataId, out FGameDataRow? mapRow))
            {
                continue;
            }

            float maxX = GetFloat(areaRow, "MaxX");
            float maxY = GetFloat(areaRow, "MaxY");
            uint worldWidth = GetUInt32(mapRow, "WorldWidth");
            uint worldHeight = GetUInt32(mapRow, "WorldHeight");
            if (maxX > worldWidth)
            {
                diagnostics.Add(
                    "GD1706",
                    "SpawnArea MaxX must be inside the referenced Map boundary.",
                    spawnAreaTable.Locate(areaRow.SourceRowNumber, areaFields["MaxX"].ColumnNumber));
            }
            if (maxY > worldHeight)
            {
                diagnostics.Add(
                    "GD1706",
                    "SpawnArea MaxY must be inside the referenced Map boundary.",
                    spawnAreaTable.Locate(areaRow.SourceRowNumber, areaFields["MaxY"].ColumnNumber));
            }
        }

        if (spawnerTable == null || monsterTable == null)
        {
            return;
        }

        Dictionary<string, FGameDataField> monsterFields = BuildFieldMap(monsterTable);
        Dictionary<string, FGameDataField> spawnerFields = BuildFieldMap(spawnerTable);
        string[] requiredMonsterFields = ["MonsterDataId", "AggroRadius"];
        string[] requiredSpawnerFields = ["MapDataId", "MonsterDataId", "SpawnAreaDataId"];
        if (!requiredMonsterFields.All(monsterFields.ContainsKey) || !requiredSpawnerFields.All(spawnerFields.ContainsKey))
        {
            return;
        }

        Dictionary<uint, FGameDataRow> monstersById = monsterTable.Rows
            .Where(row => GetUInt32(row, "MonsterDataId") != 0)
            .GroupBy(row => GetUInt32(row, "MonsterDataId"))
            .ToDictionary(group => group.Key, group => group.First());

        foreach (FGameDataRow spawnerRow in spawnerTable.Rows)
        {
            uint spawnAreaDataId = GetUInt32(spawnerRow, "SpawnAreaDataId");
            if (!areasById.TryGetValue(spawnAreaDataId, out FGameDataRow? areaRow))
            {
                continue;
            }

            uint spawnerMapDataId = GetUInt32(spawnerRow, "MapDataId");
            uint areaMapDataId = GetUInt32(areaRow, "MapDataId");
            if (spawnerMapDataId != areaMapDataId)
            {
                diagnostics.Add(
                    "GD1806",
                    "MonsterSpawner.MapDataId must match its referenced SpawnArea.MapDataId.",
                    spawnerTable.Locate(spawnerRow.SourceRowNumber, spawnerFields["MapDataId"].ColumnNumber));
            }

            uint monsterDataId = GetUInt32(spawnerRow, "MonsterDataId");
            if (!mapsById.TryGetValue(spawnerMapDataId, out FGameDataRow? mapRow) ||
                !monstersById.TryGetValue(monsterDataId, out FGameDataRow? monsterRow))
            {
                continue;
            }

            uint sectorSize = GetUInt32(mapRow, "SectorSize");
            float aggroRadius = GetFloat(monsterRow, "AggroRadius");
            if (sectorSize != 0 && aggroRadius > sectorSize)
            {
                diagnostics.Add(
                    "GD1808",
                    "MonsterSpawner requires the referenced Monster AggroRadius to fit inside one Map Sector so a 3 x 3 Sector dependency covers every interaction.",
                    spawnerTable.Locate(spawnerRow.SourceRowNumber, spawnerFields["MonsterDataId"].ColumnNumber));
            }

            float collisionRadius = GetFloat(monsterRow, "CollisionRadius");
            float areaWidth = GetFloat(areaRow, "MaxX") - GetFloat(areaRow, "MinX");
            float areaHeight = GetFloat(areaRow, "MaxY") - GetFloat(areaRow, "MinY");
            if (collisionRadius > 0.0f && (areaWidth <= collisionRadius * 2.0f || areaHeight <= collisionRadius * 2.0f))
            {
                diagnostics.Add(
                    "GD1809",
                    "MonsterSpawner requires a SpawnArea wider and taller than twice the referenced Monster CollisionRadius.",
                    spawnerTable.Locate(spawnerRow.SourceRowNumber, spawnerFields["SpawnAreaDataId"].ColumnNumber));
            }
        }
    }

    private static void ValidateCaseSensitiveTableName(
        FGameDataTable table,
        string expectedName,
        string diagnosticCode,
        FDiagnosticBag diagnostics)
    {
        if (!string.Equals(table.Name, expectedName, StringComparison.Ordinal))
        {
            diagnostics.Add(diagnosticCode, $"Table name is case-sensitive and must be exactly '{expectedName}'.", table.Locate(1, 2));
        }
    }

    private static void ValidateEnumDefinition(
        IReadOnlyDictionary<string, FGameDataEnumDefinition> definitions,
        string enumName,
        EGameDataTarget target,
        IReadOnlyList<SEnumValue> expectedValues,
        string diagnosticCode,
        FDiagnosticBag diagnostics)
    {
        if (definitions.TryGetValue(enumName, out FGameDataEnumDefinition? definition) &&
            (definition.Target != target || definition.UnderlyingType != EGameDataEnumUnderlyingType.UInt8 ||
             !definition.Values.SequenceEqual(expectedValues)))
        {
            diagnostics.Add(diagnosticCode, $"{target} uint8 enum '{enumName}' has an invalid contract.", definition.Location);
        }
    }

    private static Dictionary<string, FGameDataField> BuildFieldMap(FGameDataTable table) =>
        table.Fields
            .GroupBy(field => field.Name, StringComparer.Ordinal)
            .ToDictionary(group => group.Key, group => group.First(), StringComparer.Ordinal);

    private static bool ValidateExactFieldSet(
        FGameDataTable table,
        IReadOnlyDictionary<string, FGameDataField> fields,
        IReadOnlyList<string> requiredFields,
        string diagnosticCode,
        FDiagnosticBag diagnostics)
    {
        FGameDataField[] unsupportedFields = table.Fields
            .Where(field => !requiredFields.Contains(field.Name, StringComparer.Ordinal))
            .ToArray();
        if (unsupportedFields.Length != 0)
        {
            diagnostics.Add(
                diagnosticCode,
                $"{table.Name} table does not support additional fields: {string.Join(", ", unsupportedFields.Select(field => field.Name))}.",
                unsupportedFields[0].Location);
        }

        foreach (string requiredField in requiredFields)
        {
            if (!fields.ContainsKey(requiredField))
            {
                diagnostics.Add(diagnosticCode, $"{table.Name} table requires field '{requiredField}'.", table.Locate(13, 2));
            }
        }

        return unsupportedFields.Length == 0 && requiredFields.All(fields.ContainsKey);
    }

    private static void ValidateContractField(
        FGameDataTable table,
        FGameDataField field,
        EGameDataScalarKind expectedKind,
        EGameDataScope expectedScope,
        string diagnosticCode,
        FDiagnosticBag diagnostics,
        bool mustBePrimaryKey = false)
    {
        if (field.Type?.Kind != expectedKind)
        {
            diagnostics.Add(diagnosticCode, $"{table.Name}.{field.Name} must use type '{expectedKind.ToString().ToLowerInvariant()}'.", table.Locate(6, field.ColumnNumber));
        }
        if (field.Scope != expectedScope)
        {
            diagnostics.Add(diagnosticCode, $"{table.Name}.{field.Name} must use scope '{expectedScope}'.", table.Locate(5, field.ColumnNumber));
        }
        if (!field.Required)
        {
            diagnostics.Add(diagnosticCode, $"{table.Name}.{field.Name} must be required.", table.Locate(7, field.ColumnNumber));
        }
        if (mustBePrimaryKey && !field.IsPrimaryKey)
        {
            diagnostics.Add(diagnosticCode, $"{table.Name}.{field.Name} must be the primary key.", table.Locate(4, field.ColumnNumber));
        }
    }

    private static void ValidateRequiredReference(
        FGameDataTable table,
        FGameDataField field,
        string expectedReference,
        string diagnosticCode,
        FDiagnosticBag diagnostics)
    {
        if (!string.Equals(field.Reference, expectedReference, StringComparison.Ordinal))
        {
            diagnostics.Add(
                diagnosticCode,
                $"{table.Name}.{field.Name} must reference '{expectedReference}'.",
                table.Locate(12, field.ColumnNumber));
        }
    }

    private static uint GetUInt32(FGameDataRow row, string name) =>
        row.Values.TryGetValue(name, out object? value) && value is uint typed ? typed : 0;

    private static ulong GetUInt64(FGameDataRow row, string name) =>
        row.Values.TryGetValue(name, out object? value) && value is ulong typed ? typed : 0;

    private static float GetFloat(FGameDataRow row, string name) =>
        row.Values.TryGetValue(name, out object? value) && value is float typed ? typed : 0.0f;

    private static string GetString(FGameDataRow row, string name) =>
        row.Values.TryGetValue(name, out object? value) ? value as string ?? string.Empty : string.Empty;

    private static bool AreEquivalentTypes(FGameDataType? left, FGameDataType? right)
    {
        return left != null && right != null &&
            left.Kind == right.Kind &&
            string.Equals(left.EnumName, right.EnumName, StringComparison.Ordinal);
    }
}
