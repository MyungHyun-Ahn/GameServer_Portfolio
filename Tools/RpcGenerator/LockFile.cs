using System.Text.Json;

internal static class RpcSchemaLockManager
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
    };

    public static RpcSchemaLock ValidateAndBuild(string lockFile, IReadOnlyList<RpcSchemaDocument> documents)
    {
        RpcSchemaLock existing = Load(lockFile);
        var current = BuildCurrent(documents);
        var existingByKey = existing.Methods.ToDictionary(MethodKey);
        var documentsByService = documents.ToDictionary(document => document.Service.Id);

        foreach (RpcLockedMethod locked in existing.Methods)
        {
            if (documentsByService.TryGetValue(locked.ServiceId, out RpcSchemaDocument? document) &&
                !string.Equals(locked.Service, document.Service.Name, StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    $"RPC service id {locked.ServiceId} was renamed from '{locked.Service}' to '{document.Service.Name}'");
            }
        }

        foreach ((string key, RpcLockedMethod currentMethod) in current)
        {
            if (!existingByKey.TryGetValue(key, out RpcLockedMethod? locked))
            {
                continue;
            }

            if (locked.Reserved)
            {
                throw new InvalidOperationException(
                    $"RPC id reuse is forbidden: service {currentMethod.ServiceId}, method {currentMethod.MethodId} is reserved in {lockFile}");
            }

            if (!string.Equals(locked.Signature, currentMethod.Signature, StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    $"RPC wire signature changed for {locked.Service}.{locked.Method} ({locked.ServiceId}:{locked.MethodId}). " +
                    $"Keep the old method and allocate a new id. Locked='{locked.Signature}', Current='{currentMethod.Signature}'");
            }

            if (!string.Equals(locked.Service, currentMethod.Service, StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    $"RPC service id {locked.ServiceId} was renamed from '{locked.Service}' to '{currentMethod.Service}'");
            }
        }

        var merged = new Dictionary<string, RpcLockedMethod>(existingByKey, StringComparer.Ordinal);
        foreach ((string key, RpcLockedMethod currentMethod) in current)
        {
            merged[key] = currentMethod;
        }

        foreach ((string key, RpcLockedMethod locked) in existingByKey)
        {
            if (current.ContainsKey(key))
            {
                continue;
            }

            if (!documentsByService.TryGetValue(locked.ServiceId, out RpcSchemaDocument? document))
            {
                throw new InvalidOperationException(
                    $"RPC service {locked.Service} ({locked.ServiceId}) was removed. Keep a schema that reserves its method ids.");
            }

            if (!document.Service.ReservedMethodIds.Contains(locked.MethodId))
            {
                throw new InvalidOperationException(
                    $"Removed RPC method {locked.Service}.{locked.Method} ({locked.MethodId}) must be listed in reserved-method-ids.");
            }

            merged[key] = new RpcLockedMethod
            {
                ServiceId = locked.ServiceId,
                MethodId = locked.MethodId,
                Service = locked.Service,
                Method = locked.Method,
                Signature = locked.Signature,
                Reserved = true,
            };
        }

        return new RpcSchemaLock
        {
            Version = 1,
            Methods = merged.Values
                .OrderBy(method => method.ServiceId)
                .ThenBy(method => method.MethodId)
                .ToList(),
        };
    }

    public static string Serialize(RpcSchemaLock schemaLock)
    {
        return JsonSerializer.Serialize(schemaLock, JsonOptions).Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n') + "\n";
    }

    private static RpcSchemaLock Load(string lockFile)
    {
        if (!File.Exists(lockFile))
        {
            return new RpcSchemaLock();
        }

        RpcSchemaLock result;
        try
        {
            result = JsonSerializer.Deserialize<RpcSchemaLock>(File.ReadAllText(lockFile), JsonOptions)
                ?? throw new InvalidOperationException("Lock file is empty.");
        }
        catch (Exception exception)
        {
            throw new InvalidOperationException($"Invalid RPC lock file '{lockFile}': {exception.Message}", exception);
        }

        if (result.Version != 1)
        {
            throw new InvalidOperationException($"Unsupported RPC lock version {result.Version}: {lockFile}");
        }

        var keys = new HashSet<string>(StringComparer.Ordinal);
        var serviceNames = new Dictionary<uint, string>();
        foreach (RpcLockedMethod method in result.Methods)
        {
            if (method.ServiceId == 0 || method.MethodId == 0 || string.IsNullOrWhiteSpace(method.Signature) || !keys.Add(MethodKey(method)))
            {
                throw new InvalidOperationException($"Invalid or duplicate RPC lock entry: {lockFile}");
            }

            if (serviceNames.TryGetValue(method.ServiceId, out string? serviceName) &&
                !string.Equals(serviceName, method.Service, StringComparison.Ordinal))
            {
                throw new InvalidOperationException($"RPC lock contains conflicting names for service id {method.ServiceId}: {lockFile}");
            }

            serviceNames[method.ServiceId] = method.Service;
        }

        return result;
    }

    private static Dictionary<string, RpcLockedMethod> BuildCurrent(IReadOnlyList<RpcSchemaDocument> documents)
    {
        var result = new Dictionary<string, RpcLockedMethod>(StringComparer.Ordinal);
        foreach (RpcSchemaDocument document in documents)
        {
            var types = RpcSchemaValidator.CreateTypeRegistry(document);
            foreach (RpcMethodSchema method in document.Methods)
            {
                var parts = new List<string>
                {
                    method.RoutingKey == null ? "routing-key()" : $"routing-key({method.RoutingKey})",
                };
                if (method.Request != null)
                {
                    parts.Add($"request({CanonicalFields(method.Request.Fields, types)})");
                    parts.Add($"response({CanonicalFields(method.Response!.Fields, types)})");
                }

                if (method.Noti != null)
                {
                    parts.Add($"noti({CanonicalFields(method.Noti.Fields, types)})");
                }

                var locked = new RpcLockedMethod
                {
                    ServiceId = document.Service.Id,
                    MethodId = method.Id,
                    Service = document.Service.Name,
                    Method = method.Name,
                    Signature = string.Join("|", parts),
                    Reserved = false,
                };
                result.Add(MethodKey(locked), locked);
            }
        }

        return result;
    }

    private static string CanonicalFields(IReadOnlyList<RpcFieldSchema> fields, RpcTypeRegistry types)
    {
        return string.Join(",", fields.Select(field => $"{field.Name}:{types.Canonicalize(field.Type)}"));
    }

    private static string MethodKey(RpcLockedMethod method)
    {
        return $"{method.ServiceId}:{method.MethodId}";
    }
}
