% sample.m — MATLAB syntax-highlighting sample for the UltraCanvas demo.
% Fit a line to noisy samples and report the result.
function results = sample(n)
    if nargin < 1
        n = 50;
    end

    x = linspace(0, 10, n);
    y = 2.5 * x + 1.25 + randn(1, n) * 0.5;

    %{
      Least squares via the backslash operator:
      A * p = y, with A = [x' ones]
    %}
    A = [x' ones(n, 1)];
    p = A \ y';

    residuals = y - (p(1) * x + p(2));
    results = struct('slope', p(1), 'offset', p(2), ...
                     'rms', sqrt(mean(residuals .^ 2)));

    for k = 1:numel(fieldnames(results))
        names = fieldnames(results);
        fprintf('%-6s = %8.4f\n', names{k}, results.(names{k}));
    end

    if results.rms > 1
        warning('Fit is poor: rms %.2f', results.rms);
    end
end
