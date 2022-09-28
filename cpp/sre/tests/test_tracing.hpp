#pragma once

#include "sre/trace/trace.hpp"

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter.h>
#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/tracer_provider.h>

#include <string>

namespace trace_api      = opentelemetry::trace;
namespace trace_sdk      = opentelemetry::sdk::trace;
namespace trace_exporter = opentelemetry::exporter::trace;
namespace nostd          = opentelemetry::nostd;

using opentelemetry::exporter::memory::InMemorySpanData;
using opentelemetry::exporter::memory::InMemorySpanExporter;

static std::shared_ptr<InMemorySpanData> init_in_memory_tracing()
{
    std::unique_ptr<InMemorySpanExporter> exporter(new InMemorySpanExporter());
    std::shared_ptr<InMemorySpanData> span_data = exporter->GetData();

    auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
    std::shared_ptr<trace_api::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor));

    opentelemetry::trace::Provider::SetTracerProvider(provider);

    return span_data;
}

static void init_log_tracer()
{
    auto exporter  = trace_exporter::OStreamSpanExporterFactory::Create();
    auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
    std::shared_ptr<trace_api::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor));

    // Set the global trace provider
    trace_api::Provider::SetTracerProvider(provider);
}

static void init_external_tracer()
{
    opentelemetry::exporter::otlp::OtlpHttpExporterOptions opts;
    opts.url      = "http://localhost:4318/v1/traces";
    auto exporter = std::unique_ptr<trace_sdk::SpanExporter>(new opentelemetry::exporter::otlp::OtlpHttpExporter(opts));

    trace_sdk::BatchSpanProcessorOptions batch_options{};
    auto processor = trace_sdk::BatchSpanProcessorFactory::Create(std::move(exporter), batch_options);

    std::shared_ptr<trace_api::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor));

    // Set the global trace provider
    trace_api::Provider::SetTracerProvider(provider);
}
