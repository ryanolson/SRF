#pragma once

#include "sre/trace/trace.hpp"

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>

#include <thread>

namespace trace_api      = opentelemetry::trace;
namespace trace_sdk      = opentelemetry::sdk::trace;
namespace trace_exporter = opentelemetry::exporter::trace;
namespace nostd          = opentelemetry::nostd;

static void init_log_tracer()
{
    auto exporter  = trace_exporter::OStreamSpanExporterFactory::Create();
    auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
    std::shared_ptr<opentelemetry::trace::TracerProvider> provider =
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

    std::shared_ptr<opentelemetry::trace::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor));

    // Set the global trace provider
    trace_api::Provider::SetTracerProvider(provider);
}
