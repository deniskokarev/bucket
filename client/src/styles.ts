export const styles = {
  container: {
    flex: 1,
    backgroundColor: '#0f172a',
  },

  header: {
    paddingTop: 24,
    paddingBottom: 16,
    paddingHorizontal: 24,
    backgroundColor: '#1e293b',
    flexDirection: 'row' as const,
    alignItems: 'center' as const,
    justifyContent: 'space-between' as const,
  },

  title: {
    fontSize: 22,
    fontWeight: '700' as const,
    color: 'white',
  },

  connDot: {
    width: 12,
    height: 12,
    borderRadius: 6,
  },

  content: {
    flex: 1,
    padding: 24,
  },

  card: {
    backgroundColor: '#1e293b',
    borderRadius: 16,
    padding: 24,
    marginBottom: 20,
  },

  cardTitle: {
    fontSize: 14,
    fontWeight: '600' as const,
    color: '#94a3b8',
    textTransform: 'uppercase' as const,
    letterSpacing: 1,
    marginBottom: 16,
  },

  bigValue: {
    fontSize: 40,
    fontWeight: '800' as const,
    color: 'white',
  },

  subValue: {
    fontSize: 15,
    color: '#94a3b8',
    marginTop: 4,
  },

  row: {
    flexDirection: 'row' as const,
    alignItems: 'center' as const,
    justifyContent: 'space-between' as const,
  },

  pill: {
    paddingVertical: 6,
    paddingHorizontal: 14,
    borderRadius: 999,
  },

  pillText: {
    fontSize: 14,
    fontWeight: '700' as const,
    color: 'white',
  },

  segment: {
    flexDirection: 'row' as const,
    backgroundColor: '#0f172a',
    borderRadius: 12,
    padding: 4,
    marginTop: 8,
  },

  segmentButton: {
    flex: 1,
    paddingVertical: 14,
    borderRadius: 10,
    alignItems: 'center' as const,
  },

  segmentButtonActive: {
    backgroundColor: '#2563eb',
  },

  segmentText: {
    color: '#94a3b8',
    fontWeight: '600' as const,
    fontSize: 15,
  },

  segmentTextActive: {
    color: 'white',
  },

  primaryButton: {
    backgroundColor: '#2563eb',
    borderRadius: 14,
    paddingVertical: 18,
    alignItems: 'center' as const,
  },

  dangerButton: {
    backgroundColor: '#334155',
    borderRadius: 14,
    paddingVertical: 18,
    alignItems: 'center' as const,
  },

  forgetButton: {
    backgroundColor: '#dc2626',
    borderRadius: 14,
    paddingVertical: 18,
    alignItems: 'center' as const,
    marginTop: 8,
  },

  buttonText: {
    color: 'white',
    fontSize: 17,
    fontWeight: '700' as const,
  },

  errorText: {
    color: '#f87171',
    marginTop: 12,
    textAlign: 'center' as const,
  },

  hint: {
    color: '#64748b',
    textAlign: 'center' as const,
    marginTop: 24,
    lineHeight: 20,
  },
};
