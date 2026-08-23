import fs from "fs";
import path from "path";

import yaml from "js-yaml";

export interface OpenApiDocument {
  openapi: string;
  info: {
    title: string;
    version: string;
    description?: string;
  };
  [key: string]: unknown;
}

const openApiYamlPath = path.resolve(__dirname, "../../docs/openapi.yaml");

let cachedDocument: OpenApiDocument | null = null;

export function getOpenApiYamlPath(): string {
  return openApiYamlPath;
}

export function loadOpenApiDocument(): OpenApiDocument {
  if (cachedDocument !== null) {
    return cachedDocument;
  }

  const yamlText = fs.readFileSync(openApiYamlPath, "utf8");
  const parsed = yaml.load(yamlText);
  if (parsed === undefined || parsed === null || typeof parsed !== "object") {
    throw new Error("OpenAPI document is invalid.");
  }

  cachedDocument = parsed as OpenApiDocument;
  return cachedDocument;
}
