- **`sale sync` records a subscription's renewals.** The payment service
  names each payment (`reference`: the order for the sale, the order and
  the payment for a renewal), and the sync records a renewal as a sale of
  its own with the partner's credit, instead of skipping it as the order
  already recorded.
